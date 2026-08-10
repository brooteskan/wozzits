// tests/io/io_executor_tests.cpp
//
// The IO lane (#305 step 4a): a small group of dedicated blocking threads that
// run posted closures off the caller's thread. These pin the properties the
// wz::fs async paths and the save offload rely on -- work runs off-thread, every
// posted closure runs exactly once, and nothing queued at teardown is dropped.
//
// post() is [[nodiscard]] because a refused post means the caller still owes an
// answer to whatever was waiting on that job, so every call here checks it -- a
// discarded result is exactly the bug the attribute exists to catch.

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

        EXPECT_TRUE(io.post([&]
        {
            ran_on = std::this_thread::get_id();
            done.store(true, std::memory_order_release);
        }));

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
                EXPECT_TRUE(io.post(
                    [&] { count.fetch_add(1, std::memory_order_relaxed); }));
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
            EXPECT_TRUE(io.post([&]
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                ran.fetch_add(1, std::memory_order_relaxed);
            }));
            for (int i = 0; i < N; ++i)
                EXPECT_TRUE(io.post(
                    [&] { ran.fetch_add(1, std::memory_order_relaxed); }));
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
                        EXPECT_TRUE(io.post(
                            [&] { count.fetch_add(1, std::memory_order_relaxed); }));
                });
            for (std::thread& t : producers)
                t.join();
        }  // destructor drains
        EXPECT_EQ(count.load(), kProducers * kPer);
    }

    // ── quiesce() ───────────────────────────────────────────────────────────
    //
    // The teardown-ordering primitive the runtime's save path depends on: it must
    // make "the lane is finished" true BEFORE the caller writes the same files
    // from its own thread, without joining the threads (they must outlive
    // destroy_device, per the AMD ordering). These pin that contract, which had
    // no coverage at all.

    // The backlog is gone when quiesce() returns -- not merely dequeued, RUN.
    // Asserted before the destructor, so the drain-on-teardown path cannot be what
    // makes the count come out right.
    TEST(IoExecutor, QuiesceWaitsOutTheQueuedBacklog)
    {
        constexpr int N = 256;
        std::atomic<int> ran{ 0 };
        IoExecutor io(1);  // one thread: everything after the head is truly queued

        EXPECT_TRUE(io.post([&]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            ran.fetch_add(1, std::memory_order_relaxed);
        }));
        for (int i = 0; i < N; ++i)
            EXPECT_TRUE(io.post([&] { ran.fetch_add(1, std::memory_order_relaxed); }));

        // A SLOW tail as well as a slow head, so the last dequeue empties the queue
        // long before the work is done. Without it every queued job finished in
        // nanoseconds and the assert below held even with the in_flight_ half of
        // the barrier removed -- a test that passes against the bug it names.
        EXPECT_TRUE(io.post([&]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            ran.fetch_add(1, std::memory_order_relaxed);
        }));

        io.quiesce();
        EXPECT_EQ(ran.load(), N + 2);
    }

    // The in_flight_ half of the barrier: a job already RUNNING (queue empty) still
    // holds quiesce() until it returns. An empty-queue-only wait would sail through
    // here while the job was mid-write, which for the scene save means still
    // holding the file open -- the exact race quiesce exists to prevent.
    TEST(IoExecutor, QuiesceWaitsForAJobAlreadyRunning)
    {
        std::atomic<bool> started{ false };
        std::atomic<bool> finished{ false };
        IoExecutor io(2);

        EXPECT_TRUE(io.post([&]
        {
            started.store(true, std::memory_order_release);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            finished.store(true, std::memory_order_release);
        }));

        // Enter quiesce() with the queue already empty and the job in flight.
        while (!started.load(std::memory_order_acquire))
            std::this_thread::yield();

        io.quiesce();
        EXPECT_TRUE(finished.load(std::memory_order_acquire));
    }

    // post() after quiesce() is refused rather than silently queued behind a lane
    // that is never going to run it again. The caller checks the bool and answers
    // the waiter itself; a dropped job here is a save that reports success and
    // never wrote.
    TEST(IoExecutor, PostAfterQuiesceIsRefusedAndNeverRuns)
    {
        std::atomic<int> ran{ 0 };
        {
            IoExecutor io(2);
            io.quiesce();

            EXPECT_FALSE(io.post([&] { ran.fetch_add(1, std::memory_order_relaxed); }))
                << "a quiesced lane must refuse new work";
        }  // destructor: a refused job must not resurface in the teardown drain
        EXPECT_EQ(ran.load(), 0);
    }

    // Idempotent, as the header promises: the runtime calls it on the normal path
    // and again from the teardown guard, so a second call must return promptly
    // rather than block on a predicate nothing will signal again.
    TEST(IoExecutor, QuiesceIsIdempotent)
    {
        std::atomic<int> ran{ 0 };
        IoExecutor io(2);

        EXPECT_TRUE(io.post([&] { ran.fetch_add(1, std::memory_order_relaxed); }));
        io.quiesce();
        io.quiesce();  // must not hang
        io.quiesce();

        EXPECT_EQ(ran.load(), 1);
        EXPECT_FALSE(io.post([&] { ran.fetch_add(1, std::memory_order_relaxed); }));
    }

    // Work posted from inside a running job is refused once quiesce() has begun,
    // so the barrier cannot be extended indefinitely by the jobs it is draining.
    TEST(IoExecutor, QuiesceTerminatesWhenAJobPostsMoreWork)
    {
        std::atomic<int> ran{ 0 };
        std::atomic<int> refused{ 0 };
        IoExecutor io(1);

        EXPECT_TRUE(io.post([&]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            ran.fetch_add(1, std::memory_order_relaxed);
            // By now quiesce() has cleared accepting_, so this is refused.
            if (!io.post([&] { ran.fetch_add(1, std::memory_order_relaxed); }))
                refused.fetch_add(1, std::memory_order_relaxed);
        }));

        io.quiesce();
        EXPECT_EQ(ran.load(), 1);
        EXPECT_EQ(refused.load(), 1);
    }
}
