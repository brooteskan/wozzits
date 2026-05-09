#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

#include <logging/logger.h>
#include <logging/internal/memory_log_sink.h>
#include <logging/internal/logger_queue.h>
#include "logging_test_harness.h"

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST(ThreadedLogger, StartsAndStopsCleanly)
{
    wz::Logger logger;
    EXPECT_TRUE(wz::logging::init_logger(logger, { wz::LogLevel::Debug, false }));
    wz::logging::shutdown_logger(logger);
    EXPECT_EQ(logger.state, nullptr);
}

TEST(ThreadedLogger, InitReturnsFalseIfAlreadyInitialised)
{
    wz::Logger logger;
    wz::logging::init_logger(logger, {});
    EXPECT_FALSE(wz::logging::init_logger(logger, {}));
    wz::logging::shutdown_logger(logger);
}

TEST(ThreadedLogger, ShutdownOnUninitializedLoggerIsHarmless)
{
    wz::Logger logger;
    wz::logging::shutdown_logger(logger); // must not crash
}

TEST(ThreadedLogger, DestructorShutsDownAutomatically)
{
    wz::logging::internal::MemoryLogSink sink;

    {
        wz::Logger logger;
        wz::logging::init_logger(logger, { wz::LogLevel::Debug, false, &sink });
        logger.info("auto");
    } // ~Logger drains and frees

    EXPECT_EQ(sink.size(), 1u);
}

// ---------------------------------------------------------------------------
// Basic message delivery
// ---------------------------------------------------------------------------

TEST(ThreadedLogger, LogsSingleMessage)
{
    wz::logging::internal::MemoryLogSink sink;
    wz::Logger logger;
    wz::logging::init_logger(logger, { wz::LogLevel::Debug, false, &sink });

    wz::logging::log(logger, wz::LogLevel::Info, "hello");
    wz::logging::wait_until_idle(logger);

    auto snap = sink.snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].level, wz::LogLevel::Info);
    EXPECT_STREQ(snap[0].text, "hello");

    wz::logging::shutdown_logger(logger);
}

TEST(ThreadedLogger, AllLevelsDelivered)
{
    wz::logging::internal::MemoryLogSink sink;
    wz::Logger logger;
    wz::logging::init_logger(logger, { wz::LogLevel::Debug, false, &sink });

    logger.debug("d");
    logger.info("i");
    logger.warn("w");
    logger.error("e");
    logger.critical("c");
    wz::logging::wait_until_idle(logger);

    auto snap = sink.snapshot();
    ASSERT_EQ(snap.size(), 5u);
    EXPECT_EQ(snap[0].level, wz::LogLevel::Debug);
    EXPECT_EQ(snap[1].level, wz::LogLevel::Info);
    EXPECT_EQ(snap[2].level, wz::LogLevel::Warning);
    EXPECT_EQ(snap[3].level, wz::LogLevel::Error);
    EXPECT_EQ(snap[4].level, wz::LogLevel::Critical);

    wz::logging::shutdown_logger(logger);
}

// ---------------------------------------------------------------------------
// Min-level filter
// ---------------------------------------------------------------------------

TEST(ThreadedLogger, MinLevelFiltersLowerMessages)
{
    wz::logging::internal::MemoryLogSink sink;
    wz::Logger logger;
    wz::logging::init_logger(logger, { wz::LogLevel::Warning, false, &sink });

    logger.debug("dropped");
    logger.info("dropped");
    logger.warn("kept");
    logger.error("kept");
    wz::logging::wait_until_idle(logger);

    auto snap = sink.snapshot();
    ASSERT_EQ(snap.size(), 2u);
    EXPECT_EQ(snap[0].level, wz::LogLevel::Warning);
    EXPECT_EQ(snap[1].level, wz::LogLevel::Error);

    wz::logging::shutdown_logger(logger);
}

TEST(ThreadedLogger, LogReturnsFalseForFilteredLevel)
{
    wz::Logger logger;
    wz::logging::init_logger(logger, { wz::LogLevel::Error, false });

    EXPECT_FALSE(wz::logging::log(logger, wz::LogLevel::Debug, "below threshold"));
    EXPECT_TRUE(wz::logging::log(logger, wz::LogLevel::Error, "at threshold"));

    wz::logging::shutdown_logger(logger);
}

// ---------------------------------------------------------------------------
// Drain on shutdown
// ---------------------------------------------------------------------------

TEST(ThreadedLogger, DrainsOnShutdown)
{
    wz::logging::internal::MemoryLogSink sink;
    wz::Logger logger;
    wz::logging::init_logger(logger, { wz::LogLevel::Debug, false, &sink });

    constexpr int count = 5000;
    for (int i = 0; i < count; ++i)
        logger.info("drain");

    wz::logging::shutdown_logger(logger);

    // After shutdown the queue is fully drained — every accepted message reached the sink.
    // The queue may have dropped some under pressure, so we check submitted == sink.size().
    // (With capacity 65536 and 5000 messages, no drops are expected.)
    EXPECT_EQ(sink.size(), static_cast<std::size_t>(count));
}

// ---------------------------------------------------------------------------
// Rejection after shutdown begins
// ---------------------------------------------------------------------------

TEST(ThreadedLogger, RejectsMessagesAfterShutdown)
{
    wz::Logger logger;
    wz::logging::init_logger(logger, { wz::LogLevel::Debug, false });
    wz::logging::shutdown_logger(logger);

    EXPECT_FALSE(wz::logging::log(logger, wz::LogLevel::Info, "too late"));
}

// ---------------------------------------------------------------------------
// Multi-producer
// ---------------------------------------------------------------------------

TEST(ThreadedLogger, AcceptsManyProducerThreads)
{
    constexpr int producers  = 8;
    constexpr int per_thread = 500;

    wz::logging::internal::MemoryLogSink sink;
    wz::Logger logger;
    wz::logging::init_logger(logger, { wz::LogLevel::Debug, false, &sink });

    ThreadTestHarness harness;
    harness.spawn(producers, [&](int)
    {
        for (int i = 0; i < per_thread; ++i)
            logger.info("msg");
    });

    harness.start();
    harness.join_all();
    wz::logging::wait_until_idle(logger);

    // Queue capacity is 65536; 4000 messages fit without drops.
    EXPECT_EQ(sink.size(), static_cast<std::size_t>(producers * per_thread));

    wz::logging::shutdown_logger(logger);
}

TEST(ThreadedLogger, WaitUntilIdleBlocksUntilAllDelivered)
{
    constexpr int count = 2000;

    wz::logging::internal::MemoryLogSink sink;
    wz::Logger logger;
    wz::logging::init_logger(logger, { wz::LogLevel::Debug, false, &sink });

    for (int i = 0; i < count; ++i)
        logger.info("x");

    wz::logging::wait_until_idle(logger);

    EXPECT_EQ(sink.size(), static_cast<std::size_t>(count));

    wz::logging::shutdown_logger(logger);
}
