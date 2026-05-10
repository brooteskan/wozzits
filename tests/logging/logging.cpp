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
#include <logging/internal/memory_log_sink.h>
#include "logging_test_harness.h"

// ---------------------------------------------------------------------------
// ThreadTestHarness validation
// ---------------------------------------------------------------------------

namespace
{
    class LoggerStressTest
        : public ::testing::TestWithParam<int>
    {};

    class LoggerStressTest_B
        : public ::testing::TestWithParam<int>
    {};

    class LoggerStressTest_C
        : public ::testing::TestWithParam<int>
    {};
} // namespace

TEST_P(LoggerStressTest, NullHarnessTest)
{
    ThreadTestHarness harness;

    const int threads = GetParam();
    std::atomic<int> counter{0};

    harness.spawn(threads, [&](int)
                  { counter.fetch_add(1, std::memory_order_relaxed); });

    EXPECT_EQ(counter.load(), 0);

    harness.start();
    harness.join_all();

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

INSTANTIATE_TEST_SUITE_P(
    StressRuns,
    LoggerStressTest,
    ::testing::Values(8, 12, 16, 20));

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_P(LoggerStressTest_B, WorkerStartsOnInit)
{
    wz::logging::internal::MemoryLogSink sink;
    wz::Logger logger;
    wz::logging::init_logger(logger, { wz::LogLevel::Debug, false, &sink });

    logger.info("hello");
    wz::logging::wait_until_idle(logger);

    ASSERT_EQ(sink.size(), 1u);

    wz::logging::shutdown_logger(logger);
}

TEST_P(LoggerStressTest_B, DestructorDrainsQueue)
{
    wz::logging::internal::MemoryLogSink sink;

    {
        wz::Logger logger;
        wz::logging::init_logger(logger, { wz::LogLevel::Debug, false, &sink });
        logger.info("msg");
    } // ~Logger stops and drains

    ASSERT_EQ(sink.size(), 1u);
}

INSTANTIATE_TEST_SUITE_P(
    StressRuns,
    LoggerStressTest_B,
    ::testing::Range(0, 50));

// ---------------------------------------------------------------------------
// Stress / correctness
// ---------------------------------------------------------------------------

TEST_P(LoggerStressTest_C, MultiThreadLogging)
{
    constexpr int threads    = 8;
    constexpr int per_thread = 1000;

    wz::logging::internal::MemoryLogSink sink;
    wz::Logger logger;
    wz::logging::init_logger(logger, { wz::LogLevel::Debug, false, &sink });

    ThreadTestHarness harness;
    harness.spawn(threads, [&](int)
    {
        for (int i = 0; i < per_thread; ++i)
            logger.info("msg");
    });

    harness.start();
    harness.join_all();
    wz::logging::wait_until_idle(logger);

    EXPECT_EQ(sink.size(), static_cast<std::size_t>(threads * per_thread));

    wz::logging::shutdown_logger(logger);
}

TEST_P(LoggerStressTest_C, DestructorIsSafeUnderLoad)
{
    {
        wz::Logger logger;
        wz::logging::init_logger(logger, { wz::LogLevel::Debug, false });

        for (int i = 0; i < 10000; ++i)
            logger.info("msg");
    }

    SUCCEED();
}

TEST_P(LoggerStressTest_C, SingleThreadOrdering)
{
    wz::logging::internal::MemoryLogSink sink;
    wz::Logger logger;
    wz::logging::init_logger(logger, { wz::LogLevel::Debug, false, &sink });

    for (int i = 0; i < 1000; ++i)
        logger.info(std::to_string(i));

    wz::logging::wait_until_idle(logger);
    auto snap = sink.snapshot();
    ASSERT_EQ(snap.size(), 1000u);

    for (int i = 0; i < 1000; ++i)
        EXPECT_STREQ(snap[i].text, std::to_string(i).c_str());

    wz::logging::shutdown_logger(logger);
}

TEST_P(LoggerStressTest_C, NoMemorySinkMeansNothingCaptured)
{
    wz::logging::internal::MemoryLogSink unconfigured;
    wz::Logger logger;
    wz::logging::init_logger(logger, { wz::LogLevel::Debug, false }); // no memory sink

    for (int i = 0; i < 1000; ++i)
        logger.info("msg");

    wz::logging::wait_until_idle(logger);

    EXPECT_EQ(unconfigured.size(), 0u);

    wz::logging::shutdown_logger(logger);
}

TEST_P(LoggerStressTest_C, MemorySinkIsIsolated)
{
    wz::logging::internal::MemoryLogSink sink;
    wz::Logger logger;
    wz::logging::init_logger(logger, { wz::LogLevel::Debug, false, &sink });

    logger.info("a");
    logger.info("b");
    wz::logging::wait_until_idle(logger);

    EXPECT_EQ(sink.size(), 2u);

    wz::logging::shutdown_logger(logger);
}

TEST_P(LoggerStressTest_C, HighBurstDoesNotCrash)
{
    wz::Logger logger;
    wz::logging::init_logger(logger, { wz::LogLevel::Debug, false });

    for (int i = 0; i < 200000; ++i)
        logger.info("spam");

    wz::logging::wait_until_idle(logger);
    SUCCEED();

    wz::logging::shutdown_logger(logger);
}

TEST_P(LoggerStressTest_C, LogLevelPreserved)
{
    wz::logging::internal::MemoryLogSink sink;
    wz::Logger logger;
    wz::logging::init_logger(logger, { wz::LogLevel::Debug, false, &sink });

    logger.debug("d");
    logger.info("i");
    logger.warn("w");
    logger.error("e");
    wz::logging::wait_until_idle(logger);

    auto snap = sink.snapshot();
    ASSERT_EQ(snap.size(), 4u);
    EXPECT_EQ(snap[0].level, wz::LogLevel::Debug);
    EXPECT_EQ(snap[1].level, wz::LogLevel::Info);
    EXPECT_EQ(snap[2].level, wz::LogLevel::Warning);
    EXPECT_EQ(snap[3].level, wz::LogLevel::Error);

    wz::logging::shutdown_logger(logger);
}

TEST_P(LoggerStressTest_C, InterleavingDoesNotLoseMessages)
{
    wz::logging::internal::MemoryLogSink sink;
    wz::Logger logger;
    wz::logging::init_logger(logger, { wz::LogLevel::Debug, false, &sink });

    ThreadTestHarness h;
    h.spawn(2, [&](int)
    {
        for (int i = 0; i < 10000; ++i)
            logger.info("x");
    });

    h.start();
    h.join_all();
    wz::logging::wait_until_idle(logger);

    EXPECT_EQ(sink.size(), 20000u);

    wz::logging::shutdown_logger(logger);
}

TEST_P(LoggerStressTest_C, ThroughputSanity)
{
    wz::Logger logger;
    wz::logging::init_logger(logger, { wz::LogLevel::Debug, false });

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 200000; ++i)
        logger.info("x");

    wz::logging::wait_until_idle(logger);

    auto end = std::chrono::high_resolution_clock::now();
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_LT(ms, 2000);

    wz::logging::shutdown_logger(logger);
}

TEST_P(LoggerStressTest_C, RepeatedConstructionDestruction)
{
    for (int i = 0; i < 1000; ++i)
    {
        wz::Logger logger;
        wz::logging::init_logger(logger, { wz::LogLevel::Debug, false });

        for (int j = 0; j < 100; ++j)
            logger.info("x");

        wz::logging::shutdown_logger(logger);
    }

    SUCCEED();
}

INSTANTIATE_TEST_SUITE_P(
    StressRuns,
    LoggerStressTest_C,
    ::testing::Range(0, 50));
