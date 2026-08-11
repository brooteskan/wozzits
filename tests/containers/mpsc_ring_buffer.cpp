#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <atomic>
#include <chrono>

#include <containers/mpsc_ring_buffer.h>

#include "logging_test_harness.h"

using namespace wz::core::containers;

namespace
{

    class MPSCRingBufferTest : public ::testing::Test
    {
    protected:
        static constexpr size_t Capacity = 1024;
        MPSCRingBuffer<uint64_t, Capacity> queue;

        std::mutex output_mutex;
        std::vector<uint64_t> output;

        void push_blocking(uint64_t v)
        {
            while (!queue.try_push(v))
            {
                std::this_thread::yield();
            }
        }

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

} // namespace

TEST(ThreadTestHarness, NullHarnessTest)
{
    ThreadTestHarness harness;

    const int threads = 8;
    std::atomic<int> counter{0};

    harness.spawn(threads, [&](int)
                  { counter.fetch_add(1, std::memory_order_relaxed); });

    EXPECT_EQ(counter.load(), 0);

    harness.start();
    harness.join_all();

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

        std::this_thread::yield();

        finished.fetch_add(1, std::memory_order_relaxed); });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_EQ(entered.load(), 0);
    EXPECT_EQ(finished.load(), 0);

    harness.start();
    harness.join_all();

    EXPECT_EQ(entered.load(), threads);
    EXPECT_EQ(finished.load(), threads);
}

TEST_F(MPSCRingBufferTest, PushPopSingle)
{
    push_blocking(1);
    push_blocking(2);
    push_blocking(3);

    drain_queue();

    auto out = get_output();

    ASSERT_EQ(out.size(), 3);
    EXPECT_EQ(out[0], 1);
    EXPECT_EQ(out[1], 2);
    EXPECT_EQ(out[2], 3);
}

TEST_F(MPSCRingBufferTest, EmptyQueue)
{
    uint64_t value = 0;

    EXPECT_FALSE(queue.try_pop(value));
    EXPECT_TRUE(queue.empty());
}

TEST_F(MPSCRingBufferTest, FIFOOrderSingleProducer)
{
    const int N = 1000;

    for (uint64_t i = 0; i < N; ++i)
        push_blocking(i);

    drain_queue();

    auto out = get_output();

    ASSERT_EQ(out.size(), N);

    for (uint64_t i = 0; i < N; ++i)
        EXPECT_EQ(out[i], i);
}

TEST_F(MPSCRingBufferTest, MultiProducer)
{
    ThreadTestHarness harness;

    const int threads = 8;
    const int per_thread = 1000;

    std::atomic<bool> running{true};

    std::thread consumer([&]
                         {
        while (running.load(std::memory_order_relaxed))
        {
            drain_queue();
            std::this_thread::yield();
        }

        drain_queue(); });

    harness.spawn(threads, [&](int tid)
                  {
        for (uint64_t i = 0; i < per_thread; ++i)
        {
            uint64_t value = (uint64_t)tid << 32 | i;
            push_blocking(value);
        } });

    harness.start();
    harness.join_all();

    running.store(false);
    consumer.join();

    auto out = get_output();

    ASSERT_EQ(out.size(), threads * per_thread);

    std::unordered_set<uint64_t> set(out.begin(), out.end());
    EXPECT_EQ(set.size(), out.size());
}

TEST_F(MPSCRingBufferTest, ConcurrentProducerConsumer)
{
    ThreadTestHarness harness;

    const int producers = 6;
    const int per_thread = 2000;

    std::atomic<bool> done{false};

    std::thread consumer([&]
                         {
        while (!done.load(std::memory_order_relaxed))
        {
            drain_queue();
            std::this_thread::yield();
        }

        drain_queue(); });

    harness.spawn(producers, [&](int tid)
                  {
        for (uint64_t i = 0; i < per_thread; ++i)
        {
            uint64_t value = (uint64_t)tid << 32 | i;
            push_blocking(value);
        } });

    harness.start();
    harness.join_all();

    done.store(true);
    consumer.join();

    auto out = get_output();

    ASSERT_EQ(out.size(), producers * per_thread);

    std::unordered_set<uint64_t> set(out.begin(), out.end());
    EXPECT_EQ(set.size(), out.size());
}

// The ring's only real soak, and it was DISABLED_ because it ran effectively
// forever -- for a structural reason worth recording so it is not re-disabled the
// next time: producers used push_blocking, but the only drain ran AFTER
// join_all(), so once 8 threads had filled the 1024-slot ring every one of them
// spun on a queue nothing was draining. It was not a slow test, it was a stuck
// one, and disabling it cost the MPSC channel (which diagnostics rides, #291) its
// only multi-producer coverage.
//
// Now it drains CONCURRENTLY, like ConcurrentProducerConsumer above, so it
// finishes in milliseconds rather than spinning. Deliberately modest -- a brief
// burst, not a soak: 8 producers x 2000 still wraps the 1024-slot ring ~15 times,
// which is where a head/tail reclaim bug shows up, and that is what this is for.
// Anything heavier belongs behind an explicit opt-in, not in the default run.
TEST_F(MPSCRingBufferTest, StressTestManyProducersOverAWrappingRing)
{
    ThreadTestHarness harness;

    const int threads = 8;
    const int per_thread = 2000;

    std::atomic<bool> done{ false };
    std::thread consumer([&] {
        while (!done.load(std::memory_order_relaxed))
        {
            drain_queue();
            std::this_thread::yield();
        }
        drain_queue();  // the tail, after the last producer retired
    });

    harness.spawn(threads, [&](int tid)
                  {
        for (uint64_t i = 0; i < per_thread; ++i)
        {
            push_blocking((uint64_t)tid << 32 | i);
        } });

    harness.start();
    harness.join_all();

    done.store(true);
    consumer.join();

    auto out = get_output();

    ASSERT_EQ(out.size(), static_cast<size_t>(threads) * per_thread);

    // Every value is (tid << 32 | i) and so globally unique: a duplicate means a
    // slot was handed out twice, a shortfall means one was lost.
    std::unordered_set<uint64_t> set(out.begin(), out.end());
    EXPECT_EQ(set.size(), out.size());
}

TEST_F(MPSCRingBufferTest, CapacityLimit)
{
    for (size_t i = 0; i < Capacity; ++i)
    {
        EXPECT_TRUE(queue.try_push(i));
    }

    EXPECT_FALSE(queue.try_push(999));
}

TEST_F(MPSCRingBufferTest, PopAfterFull)
{
    for (size_t i = 0; i < Capacity; ++i)
    {
        EXPECT_TRUE(queue.try_push(i));
    }

    uint64_t v;
    EXPECT_TRUE(queue.try_pop(v));

    EXPECT_TRUE(queue.try_push(42));
}

TEST_F(MPSCRingBufferTest, TryPushBasic)
{
    EXPECT_TRUE(queue.try_push(42));

    uint64_t v;
    EXPECT_TRUE(queue.try_pop(v));
    EXPECT_EQ(v, 42);
}