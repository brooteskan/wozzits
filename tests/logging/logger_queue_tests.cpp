#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <unordered_set>
#include <atomic>

#include <logging/internal/logger_queue.h>
#include "logging_test_harness.h"

using namespace wz::logging::internal;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST(LoggerQueue, ConstructsAccepting)
{
    LoggerQueue q;
    EXPECT_TRUE(q.is_accepting());
    EXPECT_FALSE(q.is_closed());
    EXPECT_EQ(q.dropped_count(), 0u);
    EXPECT_EQ(q.submitted_count(), 0u);
}

// ---------------------------------------------------------------------------
// Single push / pop
// ---------------------------------------------------------------------------

TEST(LoggerQueue, PushPopSingleMessage)
{
    LoggerQueue q;

    EXPECT_TRUE(q.try_push(wz::LogLevel::Info, "hello"));

    LogMessage msg;
    EXPECT_TRUE(q.try_pop(msg));

    EXPECT_EQ(msg.level, wz::LogLevel::Info);
    EXPECT_STREQ(msg.text, "hello");
    EXPECT_GT(msg.sequence, 0u);
    EXPECT_GT(msg.event_ticks, 0u);
}

TEST(LoggerQueue, PopFromEmptyReturnsFalse)
{
    LoggerQueue q;
    LogMessage msg;
    EXPECT_FALSE(q.try_pop(msg));
}

// ---------------------------------------------------------------------------
// Sequence numbers
// ---------------------------------------------------------------------------

TEST(LoggerQueue, AssignsIncreasingSequenceNumbers)
{
    LoggerQueue q;

    constexpr int count = 5;
    for (int i = 0; i < count; ++i)
        q.try_push(wz::LogLevel::Info, "x");

    uint64_t prev = 0;
    for (int i = 0; i < count; ++i)
    {
        LogMessage msg;
        ASSERT_TRUE(q.try_pop(msg));
        EXPECT_GT(msg.sequence, prev);
        prev = msg.sequence;
    }
}

TEST(LoggerQueue, SequenceStartsAtOne)
{
    LoggerQueue q;
    q.try_push(wz::LogLevel::Info, "first");

    LogMessage msg;
    q.try_pop(msg);
    EXPECT_EQ(msg.sequence, 1u);
}

// ---------------------------------------------------------------------------
// Timestamps
// ---------------------------------------------------------------------------

TEST(LoggerQueue, TimestampsAreMonotonicallyNonDecreasing)
{
    LoggerQueue q;

    constexpr int count = 10;
    for (int i = 0; i < count; ++i)
        q.try_push(wz::LogLevel::Info, "t");

    uint64_t prev = 0;
    for (int i = 0; i < count; ++i)
    {
        LogMessage msg;
        ASSERT_TRUE(q.try_pop(msg));
        EXPECT_GE(msg.event_ticks, prev);
        prev = msg.event_ticks;
    }
}

// ---------------------------------------------------------------------------
// Closing behaviour
// ---------------------------------------------------------------------------

TEST(LoggerQueue, CloseRejectsNewMessages)
{
    LoggerQueue q;
    q.close();

    EXPECT_FALSE(q.is_accepting());
    EXPECT_TRUE(q.is_closed());

    EXPECT_FALSE(q.try_push(wz::LogLevel::Info, "rejected"));
    EXPECT_EQ(q.dropped_count(), 1u);
    EXPECT_EQ(q.submitted_count(), 0u);
}

TEST(LoggerQueue, CloseDoesNotDiscardQueuedMessages)
{
    LoggerQueue q;
    EXPECT_TRUE(q.try_push(wz::LogLevel::Warning, "queued before close"));

    q.close();

    LogMessage msg;
    EXPECT_TRUE(q.try_pop(msg));
    EXPECT_STREQ(msg.text, "queued before close");
    EXPECT_EQ(msg.level, wz::LogLevel::Warning);
}

TEST(LoggerQueue, SubmittedCountNotIncrementedAfterClose)
{
    LoggerQueue q;
    q.try_push(wz::LogLevel::Info, "before");
    EXPECT_EQ(q.submitted_count(), 1u);

    q.close();
    q.try_push(wz::LogLevel::Info, "after");
    EXPECT_EQ(q.submitted_count(), 1u);
    EXPECT_EQ(q.dropped_count(), 1u);
}

// ---------------------------------------------------------------------------
// Drop policy (full queue)
// ---------------------------------------------------------------------------

TEST(LoggerQueue, FullQueueDropsMessages)
{
    LoggerQueue q;

    // Fill to capacity without any consumer
    std::size_t accepted = 0;
    for (std::size_t i = 0; i <= kLoggerQueueCapacity; ++i)
    {
        if (q.try_push(wz::LogLevel::Info, "x"))
            ++accepted;
    }

    EXPECT_EQ(accepted, q.submitted_count());
    EXPECT_GE(q.dropped_count(), 1u);
    EXPECT_EQ(accepted + q.dropped_count(), kLoggerQueueCapacity + 1);
}

TEST(LoggerQueue, DroppedCountIncrementsOnFullQueue)
{
    LoggerQueue q;

    for (std::size_t i = 0; i < kLoggerQueueCapacity; ++i)
        q.try_push(wz::LogLevel::Info, "fill");

    uint64_t before = q.dropped_count();
    q.try_push(wz::LogLevel::Info, "overflow");
    EXPECT_GT(q.dropped_count(), before);
}

// ---------------------------------------------------------------------------
// Accounting invariants
// ---------------------------------------------------------------------------

TEST(LoggerQueue, SubmittedCountTracksSuccessfulPushes)
{
    LoggerQueue q;

    q.try_push(wz::LogLevel::Debug,   "a");
    q.try_push(wz::LogLevel::Info,    "b");
    q.try_push(wz::LogLevel::Warning, "c");

    EXPECT_EQ(q.submitted_count(), 3u);
    EXPECT_EQ(q.dropped_count(),   0u);
}

TEST(LoggerQueue, LevelIsPreserved)
{
    LoggerQueue q;

    q.try_push(wz::LogLevel::Debug,    "d");
    q.try_push(wz::LogLevel::Info,     "i");
    q.try_push(wz::LogLevel::Warning,  "w");
    q.try_push(wz::LogLevel::Error,    "e");
    q.try_push(wz::LogLevel::Critical, "c");

    const wz::LogLevel expected[] = {
        wz::LogLevel::Debug,
        wz::LogLevel::Info,
        wz::LogLevel::Warning,
        wz::LogLevel::Error,
        wz::LogLevel::Critical,
    };

    for (auto lvl : expected)
    {
        LogMessage msg;
        ASSERT_TRUE(q.try_pop(msg));
        EXPECT_EQ(msg.level, lvl);
    }
}

// ---------------------------------------------------------------------------
// Multi-producer
// ---------------------------------------------------------------------------

TEST(LoggerQueue, AcceptsMultipleProducerThreads)
{
    LoggerQueue q;

    constexpr int producers  = 8;
    constexpr int per_thread = 500;

    ThreadTestHarness harness;

    harness.spawn(producers, [&](int)
    {
        for (int i = 0; i < per_thread; ++i)
            q.try_push(wz::LogLevel::Info, "msg");
    });

    harness.start();
    harness.join_all();

    // Drain on the main (consumer) thread
    uint64_t popped = 0;
    LogMessage msg;
    while (q.try_pop(msg))
        ++popped;

    EXPECT_EQ(popped, q.submitted_count());
    EXPECT_EQ(popped + q.dropped_count(),
              static_cast<uint64_t>(producers * per_thread));
}

TEST(LoggerQueue, NoSequenceDuplicatesUnderContention)
{
    LoggerQueue q;

    constexpr int producers  = 4;
    constexpr int per_thread = 1000;

    ThreadTestHarness harness;

    harness.spawn(producers, [&](int)
    {
        for (int i = 0; i < per_thread; ++i)
            q.try_push(wz::LogLevel::Info, "x");
    });

    harness.start();
    harness.join_all();

    std::unordered_set<uint64_t> seen;
    LogMessage msg;
    while (q.try_pop(msg))
    {
        EXPECT_TRUE(seen.insert(msg.sequence).second)
            << "duplicate sequence: " << msg.sequence;
    }
}
