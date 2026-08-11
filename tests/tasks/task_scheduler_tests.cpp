// tests/tasks/task_scheduler_tests.cpp
//
// S1 coverage for the worker pool behind wz::tasks (#293). With a pool installed,
// run() from the main thread fans tasks onto the workers and wait() blocks until
// they finish. The headline test drives the SAME nested fork-join shape as the
// cognition collect sweep (fan child subtrees, join, combine) over a wide/deep
// tree, many times, on real threads: it must stay correct (no lost/double work),
// terminate (no deadlock), and never touch the wrong counter.
//
// Result visibility relies on the same happens-before the real callers do: a
// task's writes are published by its counter decrement (acq_rel), and wait()'s
// acquire load sees them once the counter hits zero. So a task that writes its own
// slot needs no atomic -- distinct slots are distinct memory locations.

#include <gtest/gtest.h>

#include <tasks/task.h>
#include <tasks/task_scheduler.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>
#include <vector>

namespace
{
    using wz::tasks::Counter;
    using wz::tasks::run;
    using wz::tasks::TaskScheduler;
    using wz::tasks::wait;

    // Installs a pool for the test body and restores the serial backend after.
    struct ScopedPool
    {
        explicit ScopedPool(unsigned workers) : pool(workers)
        {
            wz::tasks::set_task_scheduler(&pool);
        }
        ~ScopedPool() { wz::tasks::set_task_scheduler(nullptr); }

        TaskScheduler pool;
    };

    // Sets the force-serial flag for a test body and always clears it -- so a body
    // that fails out early cannot leave the flag on and silently serialize every
    // test that runs after it.
    struct ScopedForceSerial
    {
        ScopedForceSerial() { wz::tasks::set_force_serial(true); }
        ~ScopedForceSerial() { wz::tasks::set_force_serial(false); }
    };

    // Installs an already-constructed pool. The stall test needs custom watchdog
    // timings so it cannot use ScopedPool, but the global still must not outlive
    // the pool object -- which is exactly what an early return from a failing body
    // would leave behind, crashing the NEXT test rather than this one.
    struct ScopedInstall
    {
        explicit ScopedInstall(TaskScheduler& p)
        {
            wz::tasks::set_task_scheduler(&p);
        }
        ~ScopedInstall() { wz::tasks::set_task_scheduler(nullptr); }
    };

    // Guard for this whole file. force_serial() latches the WZ_TASKS_FORCE_SERIAL
    // env var into a function-local `static const bool` on its FIRST call, so a
    // shell that still has it set from a bisect makes run() execute inline on the
    // calling thread -- and every test below still passes, having exercised no
    // concurrency at all. ctest pins the variable off per test process (see
    // rs_add_test_group in CMakeLists.txt); this catches a direct run from such a
    // shell.
    //
    // A global environment rather than a plain TEST, because gtest skips EVERY
    // test when a global SetUp() fails. A single failing test would let the rest
    // run, and StallDetectorFiresOnALostTask genuinely deadlocks on the serial
    // backend -- run() executes its spin-until-released task inline, so the
    // release below never runs -- turning a clear diagnostic into a hang.
    class ForceSerialGuard : public ::testing::Environment
    {
    public:
        void SetUp() override
        {
            ASSERT_FALSE(wz::tasks::force_serial())
                << "WZ_TASKS_FORCE_SERIAL is set in this environment: these tests "
                   "would run on the serial backend and pass without exercising "
                   "any concurrency. Clear it and re-run.";
        }
    };

    const ::testing::Environment* const kForceSerialGuard =
        ::testing::AddGlobalTestEnvironment(new ForceSerialGuard);

    TEST(TaskScheduler, DefaultPoolHasWorkers)
    {
        TaskScheduler pool;
        EXPECT_GE(pool.worker_count(), 1u);
    }

    TEST(TaskScheduler, SingleTaskRunsOnThePool)
    {
        ScopedPool guard{ 4 };
        struct Probe { bool ran_on_worker; int ran; };
        Probe p{ false, 0 };
        Counter c;
        run(c, [](void* u)
            {
                Probe* pr = static_cast<Probe*>(u);
                pr->ran_on_worker = wz::tasks::is_worker_thread();
                ++pr->ran;
            }, &p);
        wait(c);
        EXPECT_EQ(p.ran, 1);
        // The whole point of the suite: the task really crossed onto a worker.
        // Asserting only the side effect would pass identically on the serial
        // backend, which is how a force-serial environment goes unnoticed.
        EXPECT_TRUE(p.ran_on_worker);
    }

    TEST(TaskScheduler, IndexFanOutCoversRangeOnThePool)
    {
        ScopedPool guard{ 4 };
        constexpr std::size_t N = 1000;
        std::vector<int> hits(N, 0);  // each index written by exactly one task

        Counter c;
        run(c, N, [](std::size_t i, void* u)
            { ++static_cast<int*>(u)[i]; }, hits.data());
        wait(c);

        for (std::size_t i = 0; i < N; ++i)
            EXPECT_EQ(hits[i], 1) << "index " << i;
    }

    // A tree summed by the same fork-join shape as bp_collect_subtree.
    struct Node
    {
        long long value;
        std::vector<Node> children;
    };

    struct Frame
    {
        const Node* node;
        long long result;
    };

    void sum_subtree(void* user)
    {
        Frame& f = *static_cast<Frame*>(user);
        const Node& n = *f.node;

        std::vector<Frame> kids;
        kids.reserve(n.children.size());
        for (const Node& child : n.children)
            kids.push_back(Frame{ &child, 0 });

        Counter c;
        run(c, kids.size(), [](std::size_t i, void* u)
            { sum_subtree(&static_cast<Frame*>(u)[i]); }, kids.data());
        wait(c);

        long long total = n.value;
        for (const Frame& k : kids)
            total += k.result;
        f.result = total;
    }

    Node build_tree(int depth, int branching, long long& next)
    {
        Node n{ next++, {} };
        if (depth > 0)
        {
            n.children.reserve(static_cast<std::size_t>(branching));
            for (int i = 0; i < branching; ++i)
                n.children.push_back(build_tree(depth - 1, branching, next));
        }
        return n;
    }

    TEST(TaskScheduler, NestedForkJoinOnThePoolIsCorrectAndTerminates)
    {
        ScopedPool guard{ 4 };

        long long next = 1;
        const Node root = build_tree(/*depth*/ 4, /*branching*/ 8, next);
        const long long nodes = next - 1;
        const long long expected = nodes * (nodes + 1) / 2;  // sum 1..nodes

        // Repeat so a scheduling race has many chances to surface. The main thread
        // calls sum_subtree directly, so the ROOT's children fan onto the pool
        // (real cross-thread work) while nested levels run inline on their worker.
        for (int rep = 0; rep < 50; ++rep)
        {
            Frame f{ &root, 0 };
            sum_subtree(&f);
            ASSERT_EQ(f.result, expected) << "rep " << rep;
        }
    }

    // S3: the force-serial flag bypasses the pool even when one is installed, so a
    // bug can be bisected serial-vs-parallel. run() then executes inline on the
    // CALLING thread (not a worker), and the result is still correct.
    TEST(TaskScheduler, ForceSerialBypassesThePool)
    {
        ScopedPool guard{ 4 };
        ScopedForceSerial serial;

        struct Probe { bool ran_on_worker; int value; };
        Probe p{ true, 0 };
        Counter c;
        run(c, [](void* u)
            {
                Probe* pr = static_cast<Probe*>(u);
                pr->ran_on_worker = wz::tasks::is_worker_thread();
                pr->value = 7;
            }, &p);
        wait(c);

        EXPECT_EQ(p.value, 7);
        EXPECT_FALSE(p.ran_on_worker);  // ran inline on this (non-worker) thread
    }

    // S3: the stall watchdog reports a counter that never drains -- a lost/hung
    // task that would otherwise hang the safe-island wait() forever. A task that
    // spins until released stands in for the lost task; the test releases it after
    // the watchdog fires, so nothing actually hangs.
    TEST(TaskScheduler, StallDetectorFiresOnALostTask)
    {
        wz::tasks::TaskScheduler pool{
            2, std::chrono::milliseconds{ 200 }, std::chrono::milliseconds{ 50 } };
        std::atomic<bool> stall_fired{ false };
        pool.set_stall_callback([&] { stall_fired.store(true); });
        ScopedInstall installed{ pool };

        std::atomic<bool> release{ false };
        Counter c;
        run(c, [](void* u)
            {
                std::atomic<bool>* r = static_cast<std::atomic<bool>*>(u);
                while (!r->load(std::memory_order_acquire))
                    std::this_thread::yield();
            }, &release);

        std::thread waiter([&] { wait(c); });  // the safe-island wait that hangs

        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds{ 5 };
        while (!stall_fired.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds{ 10 });

        EXPECT_TRUE(stall_fired.load());

        release.store(true, std::memory_order_release);  // let it drain -- no hang
        waiter.join();
    }

    // ── reused counters (Counter::arm) ──────────────────────────────────────
    //
    // complete()'s finisher exchanges waiter_ to a kDone sentinel and never
    // restores it, so a counter that has drained once would stay permanently
    // "done" for parking purposes. arm() clears the slot on the first batch of a
    // new round; without it every later worker-fibre park fails its registration
    // CAS and bounces onto the resumable list. The API never said a Counter is
    // single-use, and both live consumers happen to use a fresh stack Counter per
    // run/wait pair -- so the reuse path arm() exists for had no test at all.

    struct ReuseFrame
    {
        int                repetitions;
        std::size_t        fan;
        std::atomic<int>*  hits;
    };

    // Runs ON a worker (the caller fans it out), so each wait() here is a NESTED
    // wait -- the one that parks a fibre on the counter, which is what makes the
    // stale kDone sentinel matter. The same Counter serves every repetition.
    void reuse_counter_body(void* user)
    {
        ReuseFrame& f = *static_cast<ReuseFrame*>(user);
        Counter c;
        for (int r = 0; r < f.repetitions; ++r)
        {
            run(c, f.fan, [](std::size_t, void* u)
                {
                    static_cast<std::atomic<int>*>(u)->fetch_add(
                        1, std::memory_order_relaxed);
                }, f.hits);
            wait(c);
        }
    }

    // ONE worker: the configuration the livelock note in task.h names. try_run_one
    // drains resumables before deque work, so a fibre that bounces instead of
    // parking starves the very tasks that would drop the count -- a hang, not a
    // wrong answer, and only on a small machine. A regression here trips the
    // target's ctest TIMEOUT rather than failing an assertion.
    TEST(TaskScheduler, ReusedCounterNestedOnAOneWorkerPool)
    {
        ScopedPool guard{ 1 };
        ASSERT_EQ(guard.pool.worker_count(), 1u);

        std::atomic<int> hits{ 0 };
        ReuseFrame frame{ /*repetitions*/ 200, /*fan*/ 8, &hits };

        Counter outer;
        run(outer, reuse_counter_body, &frame);
        wait(outer);

        EXPECT_EQ(hits.load(), frame.repetitions * static_cast<int>(frame.fan));
    }

    // The same reuse on a multi-worker pool, where real stealing is in play.
    TEST(TaskScheduler, ReusedCounterNestedOnAMultiWorkerPool)
    {
        ScopedPool guard{ 4 };

        std::atomic<int> hits{ 0 };
        ReuseFrame frame{ /*repetitions*/ 200, /*fan*/ 8, &hits };

        Counter outer;
        run(outer, reuse_counter_body, &frame);
        wait(outer);

        EXPECT_EQ(hits.load(), frame.repetitions * static_cast<int>(frame.fan));
    }

    // Reuse from the MAIN thread: an outer wait blocks on outstanding_ and leaves
    // waiter_ null, but the finisher still stamps kDone into it -- so round two
    // starts from the same stale sentinel a nested park would trip over. Cheap,
    // and it pins arm()'s "clear on the first batch of a round" directly.
    TEST(TaskScheduler, ReusedCounterAcrossOuterWaits)
    {
        ScopedPool guard{ 2 };
        constexpr std::size_t N = 64;
        constexpr int kRounds = 200;

        std::atomic<int> hits{ 0 };
        Counter c;
        for (int round = 0; round < kRounds; ++round)
        {
            run(c, N, [](std::size_t, void* u)
                {
                    static_cast<std::atomic<int>*>(u)->fetch_add(
                        1, std::memory_order_relaxed);
                }, &hits);
            wait(c);
            ASSERT_EQ(hits.load(), static_cast<int>(N) * (round + 1))
                << "round " << round;
        }
    }

    // ─── Re-arm during completion ───────────────────────────────────────────
    //
    // The finisher in complete() claims the counter's parking slot and THEN
    // publishes the count. A second run() batch landing inside that window used
    // to be erased by a blind store(0): wait() returned with the new batch still
    // in flight, and its completions then wrote a Counter whose stack frame was
    // gone -- the crash class 18484cbb fixed, re-reachable through a plain
    // heterogeneous fork-join. The window is nanoseconds wide, so the test seam
    // holds the finisher open inside it while the main thread arms the second
    // batch, making the interleaving deterministic instead of a stress lottery.
    //
    // Against the blind store this test fails on b_ran (Release) or trips
    // complete()'s own `v > 0` assert when B finishes against a zeroed counter
    // (Debug) -- an abort rather than a clean failure, which is the honest
    // signal for a corrupted counter.
    struct ArmRaceState
    {
        std::atomic<int>  hook_fired{ 0 };
        std::atomic<bool> finisher_held{ false };
        std::atomic<bool> second_batch_armed{ false };
        std::atomic<int>  b_ran{ 0 };
    };

    // Restores the production (null) hook however the body leaves -- an early
    // return from a failing EXPECT must not leave a live hook pointing at a dead
    // stack frame for every test that runs after it.
    struct ScopedCompleteHook
    {
        ScopedCompleteHook(wz::tasks::CompleteHook hook, void* user)
        {
            wz::tasks::set_complete_test_hook(hook, user);
        }
        ~ScopedCompleteHook() { wz::tasks::set_complete_test_hook(nullptr, nullptr); }
    };

    // Spins on a handshake flag, giving up rather than hanging ctest's timeout.
    template <typename Pred>
    bool spin_until(Pred pred)
    {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds{ 10 };
        while (!pred())
        {
            if (std::chrono::steady_clock::now() > deadline)
                return false;
            std::this_thread::yield();
        }
        return true;
    }

    TEST(TaskScheduler, ArmDuringCompletionDoesNotDropTheSecondBatch)
    {
        // ONE worker, so there is exactly one completer and the handshake below
        // cannot be satisfied by some other thread wandering through complete().
        ScopedPool guard{ 1 };
        ArmRaceState s;

        ScopedCompleteHook hook_guard(
            [](void* u)
            {
                ArmRaceState* st = static_cast<ArmRaceState*>(u);
                // One-shot: the second batch's own completion must run to the end
                // unheld, or nothing would ever drain the counter.
                if (st->hook_fired.fetch_add(1, std::memory_order_relaxed) != 0)
                    return;
                st->finisher_held.store(true, std::memory_order_release);
                spin_until([st]
                    { return st->second_batch_armed.load(std::memory_order_acquire); });
            },
            &s);

        Counter c;
        run(c, [](void*) {}, nullptr);  // batch A: arms the counter to 1

        // A's completion is now parked inside the finisher, past the waiter_ claim
        // and before the publish.
        ASSERT_TRUE(spin_until([&s]
            { return s.finisher_held.load(std::memory_order_acquire); }))
            << "the completion hook never fired -- task A never ran";

        run(c, [](void* u)  // batch B: arms 1 -> 2 underneath the held finisher
            {
                static_cast<ArmRaceState*>(u)->b_ran.fetch_add(
                    1, std::memory_order_relaxed);
            }, &s);
        s.second_batch_armed.store(true, std::memory_order_release);

        wait(c);

        // The contract: wait() joins BOTH batches. Reading b_ran after wait() is
        // exactly what a real caller does with a fork-join result.
        EXPECT_EQ(s.b_ran.load(std::memory_order_relaxed), 1);
    }

    // The same shape without the seam, on real threads: several run() batches
    // stacked on one counter and joined by a single wait(). Each round re-runs the
    // arm-vs-finisher race unsynchronized, so a regression that only misbehaves on
    // a narrower interleaving than the seam builds still has many chances to show.
    TEST(TaskScheduler, HeterogeneousBatchesJoinOnOneCounter)
    {
        ScopedPool guard{ 4 };
        constexpr int kRounds = 2000;

        std::atomic<int> hits{ 0 };
        for (int round = 0; round < kRounds; ++round)
        {
            Counter c;
            run(c, [](void* u)
                { static_cast<std::atomic<int>*>(u)->fetch_add(
                      1, std::memory_order_relaxed); }, &hits);
            run(c, 4, [](std::size_t, void* u)
                { static_cast<std::atomic<int>*>(u)->fetch_add(
                      1, std::memory_order_relaxed); }, &hits);
            run(c, [](void* u)
                { static_cast<std::atomic<int>*>(u)->fetch_add(
                      1, std::memory_order_relaxed); }, &hits);
            wait(c);
            ASSERT_EQ(hits.load(std::memory_order_relaxed), 6 * (round + 1))
                << "round " << round;
        }
    }

    // Concurrent PRODUCERS on the injection queue. Every submit from a non-worker
    // goes through the MPSC injection path (#304), and until now every test drove
    // it from a single thread -- the "MP" half was uncovered. Each thread owns its
    // own counter and its own slice of the result vector, so a lost or duplicated
    // injection shows up as a wrong count in that slice.
    TEST(TaskScheduler, ConcurrentSubmittersShareTheInjectionQueue)
    {
        ScopedPool guard{ 4 };
        constexpr int kSubmitters = 4;
        constexpr int kRounds = 200;
        constexpr std::size_t kFan = 16;

        std::vector<std::vector<int>> hits(
            kSubmitters, std::vector<int>(kFan, 0));

        std::vector<std::thread> submitters;
        submitters.reserve(kSubmitters);
        for (int t = 0; t < kSubmitters; ++t)
        {
            submitters.emplace_back([&hits, t]
                {
                    for (int round = 0; round < kRounds; ++round)
                    {
                        Counter c;
                        run(c, kFan, [](std::size_t i, void* u)
                            { ++static_cast<int*>(u)[i]; }, hits[t].data());
                        wait(c);
                    }
                });
        }
        for (std::thread& th : submitters)
            th.join();

        for (int t = 0; t < kSubmitters; ++t)
            for (std::size_t i = 0; i < kFan; ++i)
                EXPECT_EQ(hits[t][i], kRounds) << "submitter " << t << " index " << i;
    }

    // The existing nested fork-join shape, but with a single worker: every nested
    // wait must park and be resumed by the same thread that parked it, with no
    // second worker able to pick up the slack.
    TEST(TaskScheduler, NestedForkJoinOnAOneWorkerPool)
    {
        ScopedPool guard{ 1 };

        long long next = 1;
        const Node root = build_tree(/*depth*/ 3, /*branching*/ 4, next);
        const long long nodes = next - 1;
        const long long expected = nodes * (nodes + 1) / 2;

        for (int rep = 0; rep < 20; ++rep)
        {
            Frame f{ &root, 0 };
            sum_subtree(&f);
            ASSERT_EQ(f.result, expected) << "rep " << rep;
        }
    }
}
