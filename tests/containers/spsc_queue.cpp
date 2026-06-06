#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <algorithm>
#include <atomic>
#include <random>
#include <chrono>

#include <containers/spsc_queue.h>
#include "spsc_test_harness.h"

using namespace wz::core::containers;

namespace
{
    struct SPSCStressParams
    {
        size_t capacity;
        size_t item_count;
    };

    struct SPSCBenchParams
    {
        size_t capacity;
        size_t item_count;
    };

    class SPSCQueueTest : public ::testing::Test
    {
    protected:
        SPSCQueue<uint64_t> queue{1024};

        std::vector<uint64_t> output;

        void push_output(uint64_t v)
        {
            output.push_back(v);
        }

        std::vector<uint64_t> get_output()
        {
            return output;
        }
    };

    class SPSCQueueStressTest : public ::testing::TestWithParam<SPSCStressParams>
    {
    protected:
        std::vector<uint64_t> output;
    };

    class SPSCQueueBench : public ::testing::TestWithParam<SPSCBenchParams>
    {
    };
}

TEST_F(SPSCQueueTest, SingleProducerSingleConsumer_FifoIntegrity)
{
    ThreadTestHarness harness;

    const uint64_t N = 100000;
    std::atomic<bool> producer_done = false;

    harness.spawn(2, [&](size_t id)
                  {
        if (id == 0)
        {
            // PRODUCER (only writer)
            for (uint64_t i = 0; i < N; ++i)
            {
                while (!queue.push(i))
                {
                    // backoff / spin
                }
            }

            producer_done.store(true, std::memory_order_release);
        }
        else
        {
            // CONSUMER (only reader)
            uint64_t value;
            uint64_t expected = 0;

            while (!producer_done.load(std::memory_order_acquire) || !queue.empty())
            {
                if (queue.try_pop(value))
                {
                    push_output(value);
                }
            }
        } });

    harness.start();
    harness.join_all();

    auto out = get_output();

    ASSERT_EQ(out.size(), N);

    for (uint64_t i = 0; i < N; ++i)
    {
        EXPECT_EQ(out[i], i);
    }
}

TEST_P(SPSCQueueStressTest, FIFO_Stress)
{
    const auto &param = GetParam();

    SPSCQueue<uint64_t> queue(param.capacity);
    ThreadTestHarness harness;

    std::atomic<bool> producer_done = false;

    harness.spawn(2, [&](size_t id)
                  {
        if (id == 0)
        {
            // PRODUCER
            for (uint64_t i = 0; i < param.item_count; ++i)
            {
                while (!queue.push(i)) { /* spin */ }
            }

            producer_done.store(true, std::memory_order_release);
        }
        else
        {
            // CONSUMER
            uint64_t value;

            while (!producer_done.load(std::memory_order_acquire) || !queue.empty())
            {
                if (queue.try_pop(value))
                {
                    output.push_back(value);
                }
            }
        } });

    harness.start();
    harness.join_all();

    ASSERT_EQ(output.size(), param.item_count);

    for (size_t i = 0; i < output.size(); ++i)
    {
        EXPECT_EQ(output[i], i);
    }
}

INSTANTIATE_TEST_SUITE_P(
    SPSCStressVariants,
    SPSCQueueStressTest,
    ::testing::Values(
        SPSCStressParams{8, 1000},      // tiny buffer, heavy churn
        SPSCStressParams{64, 100000},   // realistic event queue
        SPSCStressParams{1024, 500000}, // stable high-throughput
        SPSCStressParams{4096, 2000000} // large buffer endurance
        ));

TEST_P(SPSCQueueBench, Throughput)
{
    const auto &param = GetParam();

    SPSCQueue<uint64_t> queue(param.capacity);
    ThreadTestHarness harness;

    std::atomic<bool> producer_done = false;

    auto start = std::chrono::high_resolution_clock::now();

    harness.spawn(2, [&](size_t id)
                  {
        if (id == 0)
        {
            // PRODUCER
            for (uint64_t i = 0; i < param.item_count; ++i)
            {
                while (!queue.push(i)) { /* spin */ }
            }

            producer_done.store(true, std::memory_order_release);
        }
        else
        {
            // CONSUMER
            uint64_t value;
            uint64_t consumed = 0;

            while (!producer_done.load(std::memory_order_acquire) || !queue.empty())
            {
                if (queue.try_pop(value))
                {
                    ++consumed;
                }
            }
        } });

    harness.start();
    harness.join_all();

    auto end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> elapsed = end - start;

    double seconds = elapsed.count();
    double ops_per_sec = param.item_count / seconds;

    std::cout << "[SPSC BENCH]\n"
              << "  capacity: " << param.capacity << "\n"
              << "  items:    " << param.item_count << "\n"
              << "  time:     " << seconds << "s\n"
              << "  ops/sec:  " << ops_per_sec << "\n";
}

INSTANTIATE_TEST_SUITE_P(
    SPSCBenchmark,
    SPSCQueueBench,
    ::testing::Values(
        SPSCBenchParams{8, 1000000},     // worst-case churn
        SPSCBenchParams{64, 5000000},    // realistic event load
        SPSCBenchParams{1024, 10000000}, // stable throughput test
        SPSCBenchParams{4096, 20000000}, // sustained pipeline stress
        SPSCBenchParams{8192, 40000000}));