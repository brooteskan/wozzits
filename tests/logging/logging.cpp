#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <algorithm>
#include <atomic>
#include <random>
#include <chrono>

#include <logging/logger.h>
#include "logging_test_harness.h"

using namespace wz;

namespace
{

    class LoggerStressTest
        : public ::testing::TestWithParam<int>
    {
    };

    class LoggerStressTest_B
        : public ::testing::TestWithParam<int>
    {
    };

    class LoggerStressTest_C
        : public ::testing::TestWithParam<int>
    {
    };
} // namespace

TEST_P(LoggerStressTest, NullHarnessTest)
{
    ThreadTestHarness harness;

    const int threads = GetParam();
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

TEST_P(LoggerStressTest, BarrierCorrectness)
{
    ThreadTestHarness harness;

    const int threads = GetParam();

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

INSTANTIATE_TEST_SUITE_P(
    StressRuns,
    LoggerStressTest,
    ::testing::Values(
        8,
        12,
        16,
        20));

TEST_P(LoggerStressTest_B, WorkerStartsAutomatically)
{
    wz::Logger logger;
    logger.set_callback(LogSinkType::Buffer);
    logger.info("hello");

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto buffer = logger.snapshot_memory();
    ASSERT_EQ(buffer.size(), 1);
}

TEST_P(LoggerStressTest_B, WorkerStopsOnDestruction)
{
    std::vector<LogEvent> snapshot;

    {
        wz::Logger logger;
        logger.set_callback(LogSinkType::Buffer);

        logger.info("msg");

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        snapshot = logger.snapshot_memory();
    }

    ASSERT_EQ(snapshot.size(), 1);
}

INSTANTIATE_TEST_SUITE_P(
    StressRuns,
    LoggerStressTest_B,
    ::testing::Range(0, 50));

TEST_P(LoggerStressTest_C, MultiThreadLogging)
{
    wz::Logger logger;

    constexpr int threads = 8;
    constexpr int per_thread = 1000;

    logger.set_callback(LogSinkType::Buffer);

    ThreadTestHarness harness;

    harness.spawn(threads, [&](int)
                  {
        for (int i = 0; i < per_thread; ++i)
            logger.info("msg"); });

    harness.start();
    harness.join_all();

    logger.wait_until_idle(); // 🔥 key fix

    auto buffer = logger.snapshot_memory();

    EXPECT_EQ(buffer.size(), threads * per_thread);
}

TEST_P(LoggerStressTest_C, DestructorIsSafeUnderLoad)
{
    {
        wz::Logger logger;
        logger.set_callback(LogSinkType::Buffer);

        for (int i = 0; i < 10000; ++i)
            logger.info("msg");
    }

    SUCCEED(); // if it leaks or crashes, test fails implicitly
}

TEST_P(LoggerStressTest_C, SingleThreadOrdering)
{
    wz::Logger logger;
    logger.set_callback(LogSinkType::Buffer);

    for (int i = 0; i < 1000; ++i)
        logger.info(std::to_string(i));

    logger.wait_until_idle();

    auto buffer = logger.snapshot_memory();

    ASSERT_EQ(buffer.size(), 1000);

    for (int i = 0; i < 1000; ++i)
        EXPECT_EQ(buffer[i].message, std::to_string(i));
}

TEST_P(LoggerStressTest_C, NullSinkDropsMessages)
{
    wz::Logger logger;
    logger.set_callback(LogSinkType::Null);

    for (int i = 0; i < 1000; ++i)
        logger.info("msg");

    logger.wait_until_idle();

    auto buffer = logger.snapshot_memory();
    EXPECT_TRUE(buffer.empty());
}

TEST_P(LoggerStressTest_C, BufferSinkIsIsolated)
{
    wz::Logger logger;
    logger.set_callback(LogSinkType::Buffer);

    logger.info("a");
    logger.info("b");

    logger.wait_until_idle();

    auto buffer = logger.snapshot_memory();
    EXPECT_EQ(buffer.size(), 2);
}

TEST_P(LoggerStressTest_C, HighBurstDoesNotCrash)
{
    wz::Logger logger;
    logger.set_callback(LogSinkType::Buffer);

    for (int i = 0; i < 200000; ++i)
        logger.info("spam");

    logger.wait_until_idle();

    SUCCEED();
}

TEST_P(LoggerStressTest_C, LogLevelPreserved)
{
    wz::Logger logger;
    logger.set_callback(LogSinkType::Buffer);

    logger.debug("d");
    logger.info("i");
    logger.warn("w");
    logger.error("e");

    logger.wait_until_idle();

    auto buffer = logger.snapshot_memory();

    ASSERT_EQ(buffer.size(), 4);
    EXPECT_EQ(buffer[0].level, LogLevel::Debug);
    EXPECT_EQ(buffer[1].level, LogLevel::Info);
    EXPECT_EQ(buffer[2].level, LogLevel::Warning);
    EXPECT_EQ(buffer[3].level, LogLevel::Error);
}

TEST_P(LoggerStressTest_C, InterleavingDoesNotLoseMessages)
{
    wz::Logger logger;
    logger.set_callback(LogSinkType::Buffer);

    ThreadTestHarness h;

    h.spawn(2, [&](int)
            {
        for (int i = 0; i < 10000; ++i)
            logger.info("x"); });

    h.start();
    h.join_all();

    logger.wait_until_idle();

    auto buffer = logger.snapshot_memory();
    EXPECT_EQ(buffer.size(), 20000);
}

TEST_P(LoggerStressTest_C, ThroughputSanity)
{
    wz::Logger logger;
    logger.set_callback(LogSinkType::Null);

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 200000; ++i)
        logger.info("x");

    logger.wait_until_idle();

    auto end = std::chrono::high_resolution_clock::now();

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    EXPECT_LT(ms, 2000); // example threshold
}

TEST_P(LoggerStressTest_C, RepeatedConstructionDestruction)
{
    for (int i = 0; i < 1000; ++i)
    {
        wz::Logger logger;
        logger.set_callback(LogSinkType::Null);

        for (int j = 0; j < 100; ++j)
            logger.info("x");
    }

    SUCCEED();
}

INSTANTIATE_TEST_SUITE_P(
    StressRuns,
    LoggerStressTest_C,
    ::testing::Range(0, 50));