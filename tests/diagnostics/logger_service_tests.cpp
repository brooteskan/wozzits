// tests/diagnostics/logger_service_tests.cpp
//
// The LoggerService lane (#291 / #305 step 4d): a cold thread that drains published
// DiagnosticRecords into its aggregate on a cadence and hands them to a reporter.
// These pin the lane mechanics (publish -> drain -> report, the once-per-report
// delta, teardown draining, and the quiesce() logger-lifetime ordering) plus the
// seam and the generic logging reporter's format.
//
// Every lane here is built with a 1-hour cadence so the ONLY thing that triggers a
// report is an explicit flush_now() -- the cadence timer never fires mid-test, so
// the reporter is driven deterministically and the captured observations are safe
// to read (flush_now establishes the happens-before).

#include <gtest/gtest.h>

#include <diagnostics/diagnostic_sink.h>
#include <diagnostics/logger_service.h>

#include <logging/logger.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    using wz::diag::DiagnosticAggregate;
    using wz::diag::DiagnosticRecord;
    using wz::diag::DiagnosticSeverity;
    using wz::diag::DiagnosticSource;
    using wz::diag::LoggerServiceLane;

    constexpr auto kNoAutoCadence = std::chrono::hours(1);

    DiagnosticRecord rec(uint32_t id, uint32_t occ = 1, uint16_t pass = 0)
    {
        DiagnosticRecord r;
        r.id = id;
        r.occurrences = occ;
        r.pass = pass;
        r.severity = DiagnosticSeverity::Warning;
        r.source = DiagnosticSource::D3D12;
        return r;
    }

    // A reporter that records what each report cycle saw (unreported entries),
    // marking them reported so the next cycle only sees new occurrences.
    struct Observations
    {
        int report_calls = 0;
        std::vector<std::pair<uint32_t, uint64_t>> last;  // (id, running count)
    };

    wz::diag::DiagnosticReporter recording_reporter(Observations& obs)
    {
        return [&obs](DiagnosticAggregate& state) {
            ++obs.report_calls;
            obs.last.clear();
            for (std::size_t i = 0; i < state.size(); ++i) {
                const auto& e = state.entry(i);
                if (e.unreported() == 0) {
                    continue;
                }
                obs.last.emplace_back(e.id, e.count);
                state.mark_reported(i);
            }
        };
    }

    // Installs the process-wide sink for a test body and always clears it. The
    // global must never outlive the lane it points at: a body that leaves early
    // would strand a dangling sink that the NEXT test's publish() writes through,
    // failing somewhere unrelated to the test that actually broke.
    struct ScopedSink
    {
        explicit ScopedSink(wz::diag::IDiagnosticSink* sink)
        {
            wz::diag::set_diagnostic_sink(sink);
        }
        ~ScopedSink() { wz::diag::set_diagnostic_sink(nullptr); }
    };
}

// ── seam ────────────────────────────────────────────────────────────────────

TEST(DiagnosticSink, IsInertUntilInstalledThenRoundTrips)
{
    EXPECT_EQ(wz::diag::get_diagnostic_sink(), nullptr);

    Observations obs;
    auto lane = std::make_unique<LoggerServiceLane>(
        recording_reporter(obs), kNoAutoCadence);

    {
        ScopedSink installed{ lane.get() };
        EXPECT_EQ(wz::diag::get_diagnostic_sink(), lane.get());
    }
    EXPECT_EQ(wz::diag::get_diagnostic_sink(), nullptr);
}

// ── lane mechanics ───────────────────────────────────────────────────────────

TEST(LoggerServiceLane, PublishThenFlushReportsTheAggregate)
{
    Observations obs;
    auto lane = std::make_unique<LoggerServiceLane>(
        recording_reporter(obs), kNoAutoCadence);

    lane->publish(rec(820, 1000));
    lane->publish(rec(820, 584));   // same id -> deduped to one entry, exact total
    lane->publish(rec(999, 3));
    lane->flush_now();

    EXPECT_EQ(obs.report_calls, 1);
    ASSERT_EQ(obs.last.size(), 2u);
    EXPECT_EQ(obs.last[0], std::make_pair(uint32_t{820}, uint64_t{1584}));
    EXPECT_EQ(obs.last[1], std::make_pair(uint32_t{999}, uint64_t{3}));
}

TEST(LoggerServiceLane, ReportsOnlyNewOccurrencesSinceTheLastReport)
{
    Observations obs;
    auto lane = std::make_unique<LoggerServiceLane>(
        recording_reporter(obs), kNoAutoCadence);

    lane->publish(rec(820, 100));
    lane->flush_now();
    ASSERT_EQ(obs.last.size(), 1u);
    EXPECT_EQ(obs.last[0].second, 100u);

    // Nothing new -> a flush reports the cycle but lists no entries.
    lane->flush_now();
    EXPECT_EQ(obs.report_calls, 2);
    EXPECT_TRUE(obs.last.empty());

    // New occurrences of the same id -> reported again with the running total.
    lane->publish(rec(820, 5));
    lane->flush_now();
    ASSERT_EQ(obs.last.size(), 1u);
    EXPECT_EQ(obs.last[0].second, 105u);
}

TEST(LoggerServiceLane, TeardownDrainsAndReportsWhatWasPublished)
{
    Observations obs;
    {
        auto lane = std::make_unique<LoggerServiceLane>(
            recording_reporter(obs), kNoAutoCadence);
        lane->publish(rec(42, 7));
        // No flush -- rely on the destructor's final drain+report.
    }
    ASSERT_GE(obs.report_calls, 1);
    ASSERT_EQ(obs.last.size(), 1u);
    EXPECT_EQ(obs.last[0], std::make_pair(uint32_t{42}, uint64_t{7}));
}

TEST(LoggerServiceLane, QuiesceReportsPendingThenStopsReporting)
{
    Observations obs;
    auto lane = std::make_unique<LoggerServiceLane>(
        recording_reporter(obs), kNoAutoCadence);

    lane->publish(rec(1, 9));
    lane->quiesce();  // final flush: the pending record IS reported
    const int calls_after_quiesce = obs.report_calls;
    ASSERT_GE(calls_after_quiesce, 1);
    ASSERT_EQ(obs.last.size(), 1u);
    EXPECT_EQ(obs.last[0], std::make_pair(uint32_t{1}, uint64_t{9}));

    // After quiesce the lane still accepts + drains records but never reports --
    // this is what lets the logger be shut down before the thread is joined.
    lane->publish(rec(2, 3));
    lane->flush_now();
    EXPECT_EQ(obs.report_calls, calls_after_quiesce)
        << "no report cycle should run after quiesce()";
}

// ── the cadence timer ───────────────────────────────────────────────────────
//
// Every test above uses a 1-hour interval so ONLY flush_now drives the reporter.
// That is the right call for determinism, but it means the cadence path -- the
// one the live runtime actually runs on, and the only thing that reports when
// nobody calls flush_now -- never executed under test at all.
//
// Observed through an ATOMIC, not the plain-int Observations above: without a
// flush_now to establish happens-before, reading a non-atomic counter the worker
// thread writes is a data race, and the fp/thread sanitizers are entitled to say
// so.
namespace
{
    struct AtomicObservations
    {
        std::atomic<int> report_calls{ 0 };
        std::atomic<uint64_t> last_count{ 0 };
    };

    wz::diag::DiagnosticReporter atomic_reporter(AtomicObservations& obs)
    {
        return [&obs](DiagnosticAggregate& state) {
            for (std::size_t i = 0; i < state.size(); ++i) {
                const auto& e = state.entry(i);
                if (e.unreported() == 0) {
                    continue;
                }
                obs.last_count.store(e.count, std::memory_order_relaxed);
                state.mark_reported(i);
            }
            obs.report_calls.fetch_add(1, std::memory_order_release);
        };
    }

    // Poll an atomic until it satisfies `pred`, or the deadline passes. Returns
    // whether it got there -- so a miss FAILS the test rather than silently
    // degrading into a weaker assertion.
    template <class Pred>
    bool wait_until(Pred pred, std::chrono::milliseconds budget)
    {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        while (std::chrono::steady_clock::now() < deadline) {
            if (pred()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return pred();
    }
}

TEST(LoggerServiceLane, CadenceTimerReportsWithoutAnyFlush)
{
    AtomicObservations obs;
    auto lane = std::make_unique<LoggerServiceLane>(
        atomic_reporter(obs), std::chrono::milliseconds(10));

    lane->publish(rec(77, 4));

    // No flush_now anywhere: the cadence timer alone has to drive drain+report.
    // Generous budget so a loaded machine does not fail it; the assert is
    // "it happened", not "it happened within 10ms".
    EXPECT_TRUE(wait_until(
        [&] { return obs.report_calls.load(std::memory_order_acquire) >= 1; },
        std::chrono::seconds(5)))
        << "the cadence timer never fired a report cycle";
    EXPECT_EQ(obs.last_count.load(std::memory_order_relaxed), 4u);
}

TEST(LoggerServiceLane, CadenceKeepsReportingNewOccurrencesOverSuccessiveCycles)
{
    AtomicObservations obs;
    auto lane = std::make_unique<LoggerServiceLane>(
        atomic_reporter(obs), std::chrono::milliseconds(10));

    lane->publish(rec(5, 2));
    ASSERT_TRUE(wait_until(
        [&] { return obs.report_calls.load(std::memory_order_acquire) >= 1; },
        std::chrono::seconds(5)));
    const int after_first = obs.report_calls.load(std::memory_order_acquire);

    // A later publish must be picked up by a LATER cadence cycle -- the running
    // total keeps accumulating rather than the lane going quiet after one report.
    lane->publish(rec(5, 3));
    EXPECT_TRUE(wait_until(
        [&] { return obs.last_count.load(std::memory_order_relaxed) == 5u; },
        std::chrono::seconds(5)))
        << "a publish after the first cadence report was never picked up";
    EXPECT_GT(obs.report_calls.load(std::memory_order_acquire), after_first);
}

// ── quiesce idempotence + multi-producer publish ────────────────────────────

TEST(LoggerServiceLane, QuiesceIsIdempotent)
{
    // The runtime calls quiesce() on the normal path AND again from the teardown
    // guard, so a second call must return promptly and must not resurrect
    // reporting. (quiesce is flush_now / store / flush_now -- a second call adds
    // two more acked no-op cycles, and this is what pins that they stay no-ops.)
    Observations obs;
    auto lane = std::make_unique<LoggerServiceLane>(
        recording_reporter(obs), kNoAutoCadence);

    lane->publish(rec(11, 2));
    lane->quiesce();
    const int after_first = obs.report_calls;
    ASSERT_GE(after_first, 1);

    lane->quiesce();  // must not hang
    lane->quiesce();
    EXPECT_EQ(obs.report_calls, after_first)
        << "a repeat quiesce must not run another report cycle";

    // ...and the lane is still usable as a sink afterwards: it drains, silently.
    lane->publish(rec(12, 1));
    lane->flush_now();
    EXPECT_EQ(obs.report_calls, after_first);
}

TEST(LoggerServiceLane, ConcurrentProducersPublishWithoutLosingOccurrences)
{
    // The channel is MPSC because more than one lane may publish (#291). Only
    // single-threaded publishes were ever tested; this drives the multi-producer
    // path and checks the EXACT running total, which is the property #291 exists
    // for (an approximate count would defeat the whole feature).
    constexpr int kProducers = 4;
    constexpr int kPer = 200;
    constexpr uint32_t kId = 900;

    Observations obs;
    auto lane = std::make_unique<LoggerServiceLane>(
        recording_reporter(obs), kNoAutoCadence);

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&lane] {
            for (int i = 0; i < kPer; ++i) {
                lane->publish(rec(kId, 1));
            }
        });
    }
    for (std::thread& t : producers) {
        t.join();
    }

    lane->flush_now();
    ASSERT_EQ(obs.last.size(), 1u);
    EXPECT_EQ(obs.last[0].first, kId);
    EXPECT_EQ(obs.last[0].second,
        static_cast<uint64_t>(kProducers) * kPer)
        << "every published occurrence must land in the running total -- the "
           "channel is well under capacity here, so nothing may be dropped";
}

// ── channel drop-on-full ────────────────────────────────────────────────────

TEST(DiagnosticChannel, DropsOnFullRatherThanBlockingOrGrowing)
{
    // The back-pressure contract a hot lane depends on: try_push never blocks and
    // never allocates, so a full ring DROPS. Asserted on the real
    // DiagnosticChannel type + capacity rather than on the raw ring, so the
    // capacity constant and the record type are both covered.
    auto channel = std::make_unique<wz::diag::DiagnosticChannel>();

    for (std::size_t i = 0; i < wz::diag::kDiagnosticChannelCapacity; ++i) {
        ASSERT_TRUE(channel->try_push(rec(static_cast<uint32_t>(i))))
            << "push " << i << " within capacity must succeed";
    }
    EXPECT_FALSE(channel->try_push(rec(1)))
        << "a full diagnostic channel must drop, not block or grow";

    // Draining one frees exactly one slot -- the drop is back-pressure, not a
    // wedged channel.
    DiagnosticRecord out{};
    ASSERT_TRUE(channel->try_pop(out));
    EXPECT_TRUE(channel->try_push(rec(2)));
}

// ── generic logging reporter (format + real logger) ─────────────────────────

namespace
{
    struct CapturedLog
    {
        std::vector<std::pair<wz::LogLevel, std::string>> lines;
    };

    void capture_sink(const wz::logging::LogRecordView& record, void* user)
    {
        auto* cap = static_cast<CapturedLog*>(user);
        cap->lines.emplace_back(
            record.level, std::string(record.text, record.text_size));
    }
}

TEST(LoggerServiceReporter, FormatsExactCountAndPassThroughARealLogger)
{
    wz::Logger logger;
    ASSERT_TRUE(wz::logging::init_logger(logger, {}));
    CapturedLog cap;
    wz::logging::set_log_sink(logger, capture_sink, &cap);

    // Drive the generic reporter directly with a hand-built aggregate.
    auto reporter = wz::diag::make_logging_reporter(logger);
    DiagnosticAggregate state;
    DiagnosticRecord r = rec(820, 1584, /*pass=*/1);
    state.ingest(r);
    reporter(state);

    wz::logging::wait_until_idle(logger);
    wz::logging::shutdown_logger(logger);

    ASSERT_FALSE(cap.lines.empty());
    const std::string& line = cap.lines.back().second;
    EXPECT_EQ(cap.lines.back().first, wz::LogLevel::Warning);  // Warning severity
    EXPECT_NE(line.find("#820"), std::string::npos) << line;
    EXPECT_NE(line.find("1584"), std::string::npos) << line;   // exact total (#291)
    EXPECT_NE(line.find("pass 1"), std::string::npos) << line;
}
