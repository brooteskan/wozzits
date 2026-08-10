// tests/diagnostics/diagnostic_state_tests.cpp
//
// The cross-lane diagnostics contract (#291 / #305 step 4d), as pure data: the
// DiagnosticState aggregate a producer's DiagnosticRecords fold into, and the
// lock-free channel they ride. No threads here -- these pin the aggregation rules
// (dedup by (id,source), EXACT count, latest pass, bounded table) and the channel
// round trip; the LoggerService lane that runs this on its own thread is separate.

#include <gtest/gtest.h>

#include <diagnostics/diagnostic_channel.h>
#include <diagnostics/diagnostic_record.h>
#include <diagnostics/diagnostic_state.h>

#include <thread>

namespace
{
    using wz::diag::DiagnosticRecord;
    using wz::diag::DiagnosticSeverity;
    using wz::diag::DiagnosticSource;
    using wz::diag::DiagnosticState;

    DiagnosticRecord rec(
        uint32_t id,
        uint32_t occurrences = 1,
        uint16_t pass = 0,
        DiagnosticSeverity sev = DiagnosticSeverity::Warning,
        DiagnosticSource src = DiagnosticSource::D3D12)
    {
        DiagnosticRecord r;
        r.id = id;
        r.occurrences = occurrences;
        r.pass = pass;
        r.severity = sev;
        r.source = src;
        return r;
    }
}

// ── DiagnosticState aggregation ─────────────────────────────────────────────

TEST(DiagnosticState, IngestInsertsANewEntry)
{
    DiagnosticState<> state;
    ASSERT_TRUE(state.ingest(rec(/*id=*/820, /*occurrences=*/1)));

    ASSERT_EQ(state.size(), 1u);
    EXPECT_EQ(state.entry(0).id, 820u);
    EXPECT_EQ(state.entry(0).count, 1u);
    EXPECT_EQ(state.dropped(), 0u);
}

TEST(DiagnosticState, RepeatsOfOneIdAccumulateTheExactCount)
{
    // The whole point of #291: dedup by id but keep the TRUE total, not "suppressed
    // after N". One entry, count == the sum of every occurrence folded in.
    DiagnosticState<> state;
    state.ingest(rec(820, 1000));
    state.ingest(rec(820, 500));
    state.ingest(rec(820, 84));

    ASSERT_EQ(state.size(), 1u);          // deduped to one entry
    EXPECT_EQ(state.entry(0).count, 1584u);  // exact running total
}

TEST(DiagnosticState, DistinctIdsAndDistinctSourcesAreSeparateKeys)
{
    DiagnosticState<> state;
    state.ingest(rec(820, 2, 0, DiagnosticSeverity::Warning, DiagnosticSource::D3D12));
    state.ingest(rec(821, 3, 0, DiagnosticSeverity::Warning, DiagnosticSource::D3D12));
    // Same numeric id as the first, but a different source -> its own key.
    state.ingest(rec(820, 5, 0, DiagnosticSeverity::Error, DiagnosticSource::Engine));

    EXPECT_EQ(state.size(), 3u);
}

TEST(DiagnosticState, RefreshesLatestPassAndSeverityOnRepeat)
{
    DiagnosticState<> state;
    state.ingest(rec(820, 1, /*pass=*/1, DiagnosticSeverity::Warning));
    state.ingest(rec(820, 1, /*pass=*/7, DiagnosticSeverity::Error));

    ASSERT_EQ(state.size(), 1u);
    EXPECT_EQ(state.entry(0).last_pass, 7u);                       // most recent pass
    EXPECT_EQ(state.entry(0).severity, DiagnosticSeverity::Error); // most recent severity
    EXPECT_EQ(state.entry(0).count, 2u);
}

TEST(DiagnosticState, FullTableDropsNewKeysButStillAccumulatesKnownOnes)
{
    DiagnosticState<2> state;  // room for two distinct ids
    ASSERT_TRUE(state.ingest(rec(1, 10)));
    ASSERT_TRUE(state.ingest(rec(2, 20)));

    // A third distinct id has nowhere to go -- dropped and counted.
    EXPECT_FALSE(state.ingest(rec(3, 5)));
    EXPECT_EQ(state.dropped(), 5u);
    EXPECT_EQ(state.size(), 2u);

    // A known id still accumulates while the table is full.
    EXPECT_TRUE(state.ingest(rec(1, 7)));
    EXPECT_EQ(state.entry(0).count, 17u);
    EXPECT_EQ(state.dropped(), 5u);  // unchanged -- known key, not a drop
}

TEST(DiagnosticState, UnreportedTracksOccurrencesSinceLastReport)
{
    // The reporter logs only entries with unreported() > 0, so a steady repeat is
    // logged once per cadence with its running total, not every frame.
    DiagnosticState<> state;
    state.ingest(rec(820, 100));
    EXPECT_EQ(state.entry(0).unreported(), 100u);

    state.mark_reported(0);
    EXPECT_EQ(state.entry(0).unreported(), 0u);   // nothing new since the report
    EXPECT_EQ(state.entry(0).count, 100u);        // total preserved

    state.ingest(rec(820, 5));
    EXPECT_EQ(state.entry(0).unreported(), 5u);   // only the new occurrences
    EXPECT_EQ(state.entry(0).count, 105u);
}

TEST(DiagnosticState, ClearResetsEntriesAndDropCount)
{
    DiagnosticState<2> state;
    state.ingest(rec(1, 3));
    state.ingest(rec(2, 3));
    state.ingest(rec(3, 3));  // dropped
    ASSERT_EQ(state.dropped(), 3u);

    state.clear();
    EXPECT_EQ(state.size(), 0u);
    EXPECT_EQ(state.dropped(), 0u);
}

// ── Channel round trip (the cross-lane path, still single-threaded here) ─────

TEST(DiagnosticChannel, RecordsPushedThroughTheRingFoldIntoState)
{
    wz::diag::DiagnosticChannel channel;
    channel.try_push(rec(820, 1, /*pass=*/1));
    channel.try_push(rec(820, 1, /*pass=*/1));
    channel.try_push(rec(999, 4, /*pass=*/2));

    DiagnosticState<> state;
    DiagnosticRecord out;
    while (channel.try_pop(out)) {
        state.ingest(out);
    }

    ASSERT_EQ(state.size(), 2u);
    // id 820 seen twice (1 + 1); id 999 once with occurrences 4.
    EXPECT_EQ(state.entry(0).id, 820u);
    EXPECT_EQ(state.entry(0).count, 2u);
    EXPECT_EQ(state.entry(1).id, 999u);
    EXPECT_EQ(state.entry(1).count, 4u);
}

TEST(DiagnosticChannel, ProducerThreadCrossesToAConsumerDrainWithTheExactTotal)
{
    // The real shape: a producer thread pushes, the (single) consumer drains into
    // its lane-local state. try_push drops on a full ring, so the producer retries
    // -- no occurrence is lost, and the accumulated total is exact.
    constexpr uint32_t kPushes = 5000;
    auto channel = std::make_unique<wz::diag::DiagnosticChannel>();

    std::thread producer([&] {
        for (uint32_t i = 0; i < kPushes; ++i) {
            while (!channel->try_push(rec(/*id=*/42, /*occurrences=*/1))) {
                std::this_thread::yield();  // ring full -- let the consumer catch up
            }
        }
    });

    DiagnosticState<> state;
    DiagnosticRecord out;
    uint32_t drained = 0;
    while (drained < kPushes) {
        if (channel->try_pop(out)) {
            state.ingest(out);
            ++drained;
        }
        else {
            std::this_thread::yield();
        }
    }
    producer.join();

    ASSERT_EQ(state.size(), 1u);
    EXPECT_EQ(state.entry(0).id, 42u);
    EXPECT_EQ(state.entry(0).count, kPushes);  // every occurrence accounted for
}
