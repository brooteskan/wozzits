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

    TEST(TaskScheduler, DefaultPoolHasWorkers)
    {
        TaskScheduler pool;
        EXPECT_GE(pool.worker_count(), 1u);
    }

    TEST(TaskScheduler, SingleTaskRunsOnThePool)
    {
        ScopedPool guard{ 4 };
        int ran = 0;
        Counter c;
        run(c, [](void* u) { ++*static_cast<int*>(u); }, &ran);
        wait(c);
        EXPECT_EQ(ran, 1);
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
        wz::tasks::set_force_serial(true);

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

        wz::tasks::set_force_serial(false);

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
        wz::tasks::set_task_scheduler(&pool);

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
        wz::tasks::set_task_scheduler(nullptr);
    }
}
