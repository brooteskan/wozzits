#include <diagnostics/logger_service.h>

#include <logging/logger.h>

#include <string>
#include <utility>

// The LoggerService lane (#291 / #305 step 4d). See the header for why it is its
// own cold thread and for the quiesce() teardown ordering. The worker loop mirrors
// the IoExecutor's drain-before-join contract: a record published before teardown
// is still folded in.

namespace wz::diag
{
    LoggerServiceLane::LoggerServiceLane(
        DiagnosticReporter reporter,
        std::chrono::milliseconds report_interval)
        : reporter_(std::move(reporter))
        , interval_(report_interval)
    {
        thread_ = std::thread([this] { worker_main(); });
    }

    LoggerServiceLane::~LoggerServiceLane()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
        }
        wake_cv_.notify_all();
        thread_.join();  // worker does a final drain (+ report if not quiesced) first
    }

    void LoggerServiceLane::publish(const DiagnosticRecord& record)
    {
        // Non-blocking, drop on full. Deliberately no notify_all: waking the lane
        // per-publish from a hot lane would both hammer the cv and defeat "report on
        // its own cadence". The cadence timer (or a flush_now) drains.
        if (channel_.try_push(record)) {
            return;
        }
        // COUNT THE DROP. This return used to be discarded, which made the channel
        // the one lossy hop in the diagnostics path that did not account for itself
        // -- while the reporter presented its per-id totals as the exact running
        // count. The headroom argument for the ring's capacity assumes one drain per
        // frame, but drains run per PASS and the >64-distinct-ids fallback publishes
        // one record per message, so a validation storm can fill the ring inside a
        // single cadence: exactly when the numbers matter most, they were quietly
        // short. The occurrences (not the record count) are what is lost, since one
        // record can carry many.
        //
        // Relaxed: this is a pure counter with no other state ordered against it,
        // and the lane's exchange only needs to observe it eventually.
        channel_drops_.fetch_add(record.occurrences, std::memory_order_relaxed);
    }

    void LoggerServiceLane::flush_now()
    {
        uint64_t target;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            target = ++flush_request_;
        }
        wake_cv_.notify_all();

        std::unique_lock<std::mutex> lock(mutex_);
        flushed_cv_.wait(lock, [this, target] { return flush_done_ >= target; });
    }

    void LoggerServiceLane::quiesce()
    {
        // Final report while the logger is still alive (flush_now blocks until the
        // worker has run one drain+report cycle with reporting_ still true), THEN
        // stop reporting -- so the runtime can shut the logger down before this
        // lane's thread is joined without the reporter racing shutdown_logger.
        flush_now();
        reporting_.store(false, std::memory_order_release);

        // Barrier: prove no cycle is still mid-report. Without this, the store
        // above only stops cycles that have not yet reached the reporting_ load in
        // drain_and_report -- a cadence cycle that woke between the flush_now
        // returning and the store, loaded true, and was then descheduled would call
        // the reporter after the runtime moved on to shutdown_logger, which is
        // precisely the use-after-free quiesce exists to prevent (and it sits on
        // the live teardown path).
        //
        // One extra flush_now closes it because the worker is SERIAL: this
        // flush_now returns only once a cycle has acked a request issued after the
        // store, and the single worker thread cannot begin that cycle until any
        // in-flight drain_and_report has run to completion. So on return, every
        // cycle that could have loaded reporting_ as true is done, and every later
        // one loads false. Idempotent -- a second quiesce() just adds two acked
        // no-op cycles.
        flush_now();
    }

    void LoggerServiceLane::drain_and_report()
    {
        DiagnosticRecord r;
        while (channel_.try_pop(r)) {
            state_.ingest(r);
        }
        // Fold in whatever the producers could not fit. Taken AFTER the drain so a
        // drop recorded during it is attributed to this cycle rather than left for
        // the next one; exchange rather than load so nothing is counted twice.
        if (const uint64_t lost =
                channel_drops_.exchange(0, std::memory_order_relaxed);
            lost != 0) {
            state_.note_channel_drops(lost);
        }
        // After quiesce() the channel is still drained (kept from overflowing) but
        // the reporter -- the only thing that touches the logger -- is not called.
        if (reporting_.load(std::memory_order_acquire) && reporter_) {
            reporter_(state_);
        }
    }

    void LoggerServiceLane::worker_main()
    {
        for (;;) {
            uint64_t served;
            bool stopping;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                // Wake on the cadence, an explicit flush, or teardown. On a plain
                // cadence timeout the predicate is false but the wait returns anyway,
                // so a periodic drain+report still happens.
                wake_cv_.wait_for(lock, interval_, [this] {
                    return !running_ || flush_request_ > flush_done_;
                });
                stopping = !running_;
                served   = flush_request_;
            }

            drain_and_report();

            {
                std::lock_guard<std::mutex> lock(mutex_);
                flush_done_ = served;  // ack any flush up to the request we observed
            }
            flushed_cv_.notify_all();

            if (stopping) {
                break;  // the drain above was the final one; exit
            }
        }
    }

    // ── generic reporter ────────────────────────────────────────────────────

    namespace
    {
        const char* source_label(DiagnosticSource s)
        {
            switch (s) {
                case DiagnosticSource::D3D12:  return "D3D12";
                case DiagnosticSource::Engine: return "engine";
            }
            return "?";
        }

        void emit(wz::Logger& logger, DiagnosticSeverity sev, const std::string& text)
        {
            switch (sev) {
                case DiagnosticSeverity::Info:       logger.info(text);     break;
                case DiagnosticSeverity::Warning:    logger.warn(text);     break;
                case DiagnosticSeverity::Error:      logger.error(text);    break;
                case DiagnosticSeverity::Corruption: logger.critical(text); break;
            }
        }
    }

    void report_diagnostics(
        DiagnosticAggregate& state,
        wz::Logger& logger,
        const std::function<std::string(const DiagnosticAggregate::Entry&)>&
            format_line,
        ReportedLosses& last_reported)
    {
        for (std::size_t i = 0; i < state.size(); ++i) {
            const auto& e = state.entry(i);
            if (e.unreported() == 0) {
                continue;  // nothing new since the last report -- no line
            }
            emit(logger, e.severity, format_line(e));
            state.mark_reported(i);
        }
        if (state.dropped() > last_reported.table_dropped) {
            logger.warn(
                "diagnostics: table full, "
                + std::to_string(state.dropped())
                + " occurrences of untracked ids not itemized");
            last_reported.table_dropped = state.dropped();
        }
        // The other loss: records the producer could not fit into the cross-lane
        // channel. Distinct from the table cap above -- that one means too many
        // distinct ids, this one means the producer outran the report cadence --
        // and it is the reason the per-id totals above may read short. Saying so
        // is the whole point: an undercount presented as exact is worse than an
        // undercount that admits it.
        if (state.channel_dropped() > last_reported.channel_dropped) {
            logger.warn(
                "diagnostics: channel full, "
                + std::to_string(state.channel_dropped())
                + " occurrences never reached the report (totals above are short "
                  "by at least this much)");
            last_reported.channel_dropped = state.channel_dropped();
        }
    }

    DiagnosticReporter make_logging_reporter(wz::Logger& logger)
    {
        // last_reported persists across cadence calls (the lane never copies the
        // reporter between calls), so each loss note logs only when it grows.
        return [&logger, last_reported = ReportedLosses{}](
                   DiagnosticAggregate& state) mutable {
            report_diagnostics(
                state, logger,
                [](const DiagnosticAggregate::Entry& e) {
                    std::string line = source_label(e.source);
                    line += " diagnostic #";
                    line += std::to_string(e.id);
                    line += " x";
                    line += std::to_string(e.count);  // the EXACT running total (#291)
                    line += " [pass ";
                    line += std::to_string(e.last_pass);
                    line += "]";
                    return line;
                },
                last_reported);
        };
    }

} // namespace wz::diag
