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
