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

// ---------------------------------------------------------------------------
// Platform sinks (smoke — we can't intercept OS output in GTest)
// ---------------------------------------------------------------------------

TEST(ThreadedLogger, DebuggerSinkDoesNotCrash)
{
    wz::Logger logger;
    wz::logging::LoggerDesc desc;
    desc.enable_stderr_sink   = false;
    desc.enable_debugger_sink = true;
    wz::logging::init_logger(logger, desc);

    logger.debug("debugger debug");
    logger.info("debugger info");
    logger.warn("debugger warn");
    logger.error("debugger error");
    logger.critical("debugger critical");
    wz::logging::wait_until_idle(logger);

    SUCCEED();
    wz::logging::shutdown_logger(logger);
}

TEST(ThreadedLogger, ConsoleSinkDoesNotCrash)
{
    wz::Logger logger;
    wz::logging::LoggerDesc desc;
    desc.enable_stderr_sink  = false;
    desc.enable_console_sink = true;
    wz::logging::init_logger(logger, desc);

    logger.debug("console debug");
    logger.info("console info");
    logger.warn("console warn");
    logger.error("console error");
    logger.critical("console critical");
    wz::logging::wait_until_idle(logger);

    SUCCEED();
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

// ---------------------------------------------------------------------------
// Idle backoff (issue #313, B4-C10)
// ---------------------------------------------------------------------------
// The worker loop used a bare std::this_thread::yield() on an empty queue,
// which on Windows is SwitchToThread() and returns immediately when nothing
// else is ready -- i.e. a pure spin. Measured at 99.0% of one core over a 3s
// idle window with nothing being logged, for the lifetime of every engine
// process. It now spins briefly and then sleeps.
//
// CPU usage itself is not assertable without flakiness, so what is pinned here
// is the property that would break if someone "fixed" the burn by simply
// sleeping longer: a message logged after the worker has gone idle must still
// arrive promptly.

TEST(ThreadedLogger, AMessageAfterAnIdlePeriodStillArrivesPromptly)
{
    wz::logging::internal::MemoryLogSink sink;
    wz::Logger logger;
    wz::logging::init_logger(logger, { wz::LogLevel::Debug, false, &sink });

    wz::logging::log(logger, wz::LogLevel::Info, "before idle");
    wz::logging::wait_until_idle(logger);

    // Long enough that the worker has certainly left its spin phase and is in
    // the sleeping branch.
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    const auto sent = std::chrono::steady_clock::now();
    wz::logging::log(logger, wz::LogLevel::Info, "after idle");
    wz::logging::wait_until_idle(logger);
    const auto latency = std::chrono::steady_clock::now() - sent;

    auto snap = sink.snapshot();
    ASSERT_EQ(snap.size(), 2u);
    EXPECT_STREQ(snap[1].text, "after idle");

    // Generous bound: the backoff sleep is ~1ms, so anything approaching this
    // means the idle sleep has been raised to something that makes logging
    // useless for diagnosing a live system.
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(latency)
                  .count(),
              250)
        << "a message after an idle period took too long to reach the sink";

    wz::logging::shutdown_logger(logger);
}

TEST(ThreadedLogger, ABurstAfterIdleIsStillDrainedInFull)
{
    // The backoff must not cost throughput: the spin phase exists so a burst
    // drains at full speed once the first message wakes the worker.
    wz::logging::internal::MemoryLogSink sink;
    wz::Logger logger;
    wz::logging::init_logger(logger, { wz::LogLevel::Debug, false, &sink });

    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    constexpr int kBurst = 500;
    for (int i = 0; i < kBurst; ++i) {
        wz::logging::log(logger, wz::LogLevel::Info, "burst");
    }
    wz::logging::wait_until_idle(logger);

    EXPECT_EQ(sink.snapshot().size(), static_cast<size_t>(kBurst));

    wz::logging::shutdown_logger(logger);
}
