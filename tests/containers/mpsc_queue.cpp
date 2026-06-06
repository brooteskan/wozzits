#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <algorithm>
#include <atomic>
#include <random>
#include <chrono>

#include <containers/mpsc_queue.h>

#include "logging_test_harness.h"

using namespace wz::core::containers;

namespace
{
    class MPSCQueueTest : public ::testing::Test
    {
    protected:
        MPSCQueue<uint64_t> queue;

        std::mutex output_mutex;
        std::vector<uint64_t> output;

        void push_output(uint64_t v)
        {
            std::lock_guard<std::mutex> lock(output_mutex);
            output.push_back(v);
        }

        void drain_queue()
        {
            uint64_t value;
            while (queue.try_pop(value))
            {
                push_output(value);
            }
        }

        std::vector<uint64_t> get_output()
        {
            std::lock_guard<std::mutex> lock(output_mutex);
            return output;
        }
    };
}

TEST(ThreadTestHarness, NullHarnessTest)
{
    ThreadTestHarness harness;

    const int threads = 8;
    std::atomic<int> counter{0};

    harness.spawn(threads, [&](int)
                  { counter.fetch_add(1, std::memory_order_relaxed); });

    // Threads should be waiting at the start barrier
    EXPECT_EQ(counter.load(), 0);

    harness.start();
    harness.join_all();

    // After start + join, all threads must have run exactly once
    EXPECT_EQ(counter.load(), threads);
}

TEST(ThreadTestHarness, BarrierCorrectness)
{
    ThreadTestHarness harness;

    const int threads = 16;

    std::atomic<int> entered{0};
    std::atomic<int> finished{0};

    harness.spawn(threads, [&](int)
                  {
        entered.fetch_add(1, std::memory_order_relaxed);

        // simulate work
        std::this_thread::yield();

        finished.fetch_add(1, std::memory_order_relaxed); });

    // Give threads time to start and block on the barrier
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // If the harness is correct, none should have entered yet
    EXPECT_EQ(entered.load(), 0);
    EXPECT_EQ(finished.load(), 0);

    harness.start();
    harness.join_all();

    EXPECT_EQ(entered.load(), threads);
    EXPECT_EQ(finished.load(), threads);
}

TEST_F(MPSCQueueTest, MultipleProducers)
{
    ThreadTestHarness harness;

    const int producer_count = 8;
    const int items_per_thread = 1000;

    harness.spawn(producer_count, [&](int id)
                  {
        for (int i = 0; i < items_per_thread; ++i)
        {
            queue.push(id * items_per_thread + i);
        } });

    harness.start();
    harness.join_all();

    drain_queue();

    auto result = get_output();

    EXPECT_EQ(result.size(), producer_count * items_per_thread);
}

TEST_F(MPSCQueueTest, SingleThreadPushPop)
{
    ThreadTestHarness harness;

    queue.push(1);
    queue.push(2);
    queue.push(3);

    drain_queue();

    auto out = get_output();
    EXPECT_EQ(out.size(), 3);
    EXPECT_EQ(out[0], 1);
    EXPECT_EQ(out[1], 2);
    EXPECT_EQ(out[2], 3);
}

TEST_F(MPSCQueueTest, MultiProducerCorrectness)
{
    ThreadTestHarness harness;

    const int num_threads = 8;
    const int items_per_thread = 1000;

    harness.spawn(num_threads, [&](int t)
                  {
        for (int i = 0; i < items_per_thread; ++i)
        {
            queue.push(t * 100000 + i);
        } });

    harness.start();
    harness.join_all();

    drain_queue();

    auto out = get_output();
    EXPECT_EQ(out.size(), num_threads * items_per_thread);

    std::unordered_set<uint64_t> seen;

    for (auto v : out)
    {
        EXPECT_TRUE(seen.insert(v).second) << "Duplicate: " << v;
    }
}

TEST_F(MPSCQueueTest, FIFOApproximationSingleConsumer)
{
    const int n = 10000;

    for (int i = 0; i < n; ++i)
        queue.push(i);

    drain_queue();

    auto out = get_output();
    EXPECT_EQ(out.size(), n);

    for (int i = 1; i < n; ++i)
    {
        EXPECT_NE(out[i], out[i - 1]);
    }
}

TEST_F(MPSCQueueTest, HighContentionStress)
{
    ThreadTestHarness harness;

    const int threads = 16;
    const int per_thread = 5000;

    harness.spawn(threads, [&](int t)
                  {
        for (int i = 0; i < per_thread; ++i)
        {
            queue.push(t * 100000 + i);
        } });

    harness.start();
    harness.join_all();

    drain_queue();

    auto out = get_output();
    EXPECT_EQ(out.size(), threads * per_thread);

    std::unordered_set<uint64_t> seen;
    for (auto v : out)
        EXPECT_TRUE(seen.insert(v).second);
}

TEST_F(MPSCQueueTest, EmptyQueueBehavior)
{
    uint64_t v = -1;
    EXPECT_FALSE(queue.try_pop(v));
}

TEST_F(MPSCQueueTest, ConcurrentProducerConsumer)
{
    const int producers = 2;
    const int per_thread = 5;

    std::atomic<bool> start{false};
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    std::vector<std::thread> threads;

    // Consumer
    std::thread consumer([&]()
                         {
            while (!start.load()) {}

            uint64_t value;
            while (consumed.load() < producers * per_thread)
            {
                if (queue.try_pop(value))
                {
                    consumed++;
                }
            } });

    // Producers
    for (int t = 0; t < producers; ++t)
    {
        threads.emplace_back([&, t]()
                             {
                while (!start.load()) {}

                for (int i = 0; i < per_thread; ++i)
                {
                    queue.push(t * 100000 + i);
                    produced++;
                } });
    }

    start = true;

    for (auto &th : threads)
        th.join();

    consumer.join();

    EXPECT_EQ(produced.load(), producers * per_thread);
    EXPECT_EQ(consumed.load(), producers * per_thread);
}

TEST_F(MPSCQueueTest, FrameDrivenStress)
{
    const int producers = 8;
    const int per_thread = 200000;
    const int total = producers * per_thread;

    std::atomic<int> produced{0};

    std::vector<std::thread> threads;

    // Frame buffer (simulates EventBuffer in engine)
    std::vector<int> frame_events;
    frame_events.reserve(1024);

    std::atomic<bool> running{true};

    // -------------------------
    // PRODUCERS
    // -------------------------
    for (int t = 0; t < producers; ++t)
    {
        threads.emplace_back([&, t]()
                             {
            for (int i = 0; i < per_thread; ++i)
            {
                int value = t * per_thread + i;
                queue.push(value);

                produced.fetch_add(1, std::memory_order_relaxed);

                if ((i & 255) == 0)
                    std::this_thread::yield();
            } });
    }

    // -------------------------
    // ENGINE LOOP (FRAME DRIVEN)
    // -------------------------
    std::vector<int> all_events;
    all_events.reserve(total);

    while (true)
    {
        // simulate frame start
        frame_events.clear();

        // drain queue (THIS is your engine boundary)
        uint64_t value;
        while (queue.try_pop(value))
        {
            frame_events.push_back(value);
        }

        // "process frame"
        for (int v : frame_events)
        {
            all_events.push_back(v);
        }

        // termination condition:
        // all producers done AND queue drained
        bool all_produced =
            produced.load(std::memory_order_acquire) == total;

        bool queue_empty = queue.empty();

        if (all_produced && queue_empty)
            break;

        std::this_thread::yield();
    }

    // join producers
    for (auto &th : threads)
        th.join();

    // -------------------------
    // VALIDATION
    // -------------------------
    ASSERT_EQ(all_events.size(), total);

    std::unordered_set<int> seen;
    seen.reserve(total);

    for (uint64_t v : all_events)
    {
        EXPECT_TRUE(seen.insert(v).second)
            << "Duplicate: " << v;
    }

    EXPECT_EQ(seen.size(), total);
}

TEST_F(MPSCQueueTest, ChaosStress)
{
    const int producers = 128;
    const int duration_seconds = 180;

    std::atomic<bool> running{true};
    std::atomic<uint64_t> produced{0};
    std::atomic<uint64_t> consumed{0};

    std::vector<std::thread> threads;

    // Per-producer sequence tracking (must persist across entire run)
    std::vector<int64_t> last_seen(producers, -1);

    std::atomic<int> violations{0};

    // Consumer
    std::thread consumer([&]()
                         {
        uint64_t value;

        while (running.load(std::memory_order_acquire))
        {
            if (queue.try_pop(value))
            {
                consumed.fetch_add(1, std::memory_order_relaxed);

                uint64_t producer = value >> 48;
                uint64_t seq = value & 0x0000FFFFFFFFFFFFULL;

                if (last_seen[producer] != -1)
                {
                    if (seq != static_cast<uint64_t>(last_seen[producer] + 1))
                        violations.fetch_add(1, std::memory_order_relaxed);
                }

                last_seen[producer] = seq;
            }
            else
            {
                std::this_thread::yield();
            }
        }

        // Drain remaining items
        while (queue.try_pop(value))
        {
            consumed.fetch_add(1, std::memory_order_relaxed);

            uint64_t producer = value >> 48;
            uint64_t seq = value & 0x0000FFFFFFFFFFFFULL;

            if (last_seen[producer] != -1)
            {
                if (seq != static_cast<uint64_t>(last_seen[producer] + 1))
                    violations.fetch_add(1, std::memory_order_relaxed);
            }

            last_seen[producer] = seq;
        } });

    // Producers
    for (int t = 0; t < producers; ++t)
    {
        threads.emplace_back([&, t]()
                             {
            std::mt19937 rng(t + 1234);
            std::uniform_int_distribution<int> spin(0, 50);

            uint64_t local = 0;

            while (running.load(std::memory_order_acquire))
            {
                int burst = spin(rng);

                for (int i = 0; i < burst; ++i)
                {
                    uint64_t v = ((uint64_t)t << 48) | local++;

                    queue.push(v);
                    produced.fetch_add(1, std::memory_order_relaxed);
                }

                if (spin(rng) < 5)
                    std::this_thread::yield();
            } });
    }

    std::this_thread::sleep_for(std::chrono::seconds(duration_seconds));

    running.store(false, std::memory_order_release);

    for (auto &th : threads)
        th.join();

    consumer.join();

    uint64_t p = produced.load();
    uint64_t c = consumed.load();

    EXPECT_EQ(p, c);

    EXPECT_EQ(violations.load(), 0);
}
