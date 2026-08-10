// tests/io/io_executor_tests.cpp
//
// The IO lane (#305 step 4a): a small group of dedicated blocking threads that
// run posted closures off the caller's thread. These pin the properties the
// wz::fs async paths and the save offload rely on -- work runs off-thread, every
// posted closure runs exactly once, and nothing queued at teardown is dropped.

#include <gtest/gtest.h>

#include <io/io_executor.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace
{
    using wz::io::IoExecutor;

    TEST(IoExecutor, RunsPostedClosureOffTheCallingThread)
    {
        IoExecutor io(2);
        const std::thread::id caller = std::this_thread::get_id();
        std::thread::id ran_on;
        std::atomic<bool> done{ false };

        io.post([&]
        {
            ran_on = std::this_thread::get_id();
            done.store(true, std::memory_order_release);
        });

        while (!done.load(std::memory_order_acquire))
            std::this_thread::yield();

        EXPECT_NE(ran_on, caller);  // ran on an IO thread, not the poster
    }

    // Post many closures, then let the executor destruct. The drain-on-teardown
    // contract means every one runs before the destructor returns.
    TEST(IoExecutor, RunsEveryPostedClosureExactlyOnce)
    {
        constexpr int N = 2000;
        std::atomic<int> count{ 0 };
        {
            IoExecutor io(2);
            for (int i = 0; i < N; ++i)
                io.post([&] { count.fetch_add(1, std::memory_order_relaxed); });
        }  // destructor drains + joins
        EXPECT_EQ(count.load(), N);
    }

    // A single IO thread runs a slow first job while the rest sit queued; the
    // destructor must still drain all of them, not drop the queued tail.
    TEST(IoExecutor, DrainsQueuedWorkOnDestruction)
    {
        constexpr int N = 64;
        std::atomic<int> ran{ 0 };
        {
            IoExecutor io(1);
            io.post([&]
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                ran.fetch_add(1, std::memory_order_relaxed);
            });
            for (int i = 0; i < N; ++i)
                io.post([&] { ran.fetch_add(1, std::memory_order_relaxed); });
        }  // destructor: drain the slow job + all N queued behind it
        EXPECT_EQ(ran.load(), N + 1);
    }

    // Several producer threads hammer post() concurrently; every closure must run
    // exactly once (the queue's lock keeps multi-producer / multi-consumer sound).
    TEST(IoExecutor, ConcurrentPostersAllRunExactlyOnce)
    {
        constexpr int kProducers = 4;
        constexpr int kPer = 1000;
        std::atomic<int> count{ 0 };
        {
            IoExecutor io(3);
            std::vector<std::thread> producers;
            for (int p = 0; p < kProducers; ++p)
                producers.emplace_back([&]
                {
                    for (int i = 0; i < kPer; ++i)
                        io.post([&] { count.fetch_add(1, std::memory_order_relaxed); });
                });
            for (std::thread& t : producers)
                t.join();
        }  // destructor drains
        EXPECT_EQ(count.load(), kProducers * kPer);
    }
}
