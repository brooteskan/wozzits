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
        channel_.try_push(record);
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
    }

    void LoggerServiceLane::drain_and_report()
    {
        DiagnosticRecord r;
        while (channel_.try_pop(r)) {
            state_.ingest(r);
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

    DiagnosticReporter make_logging_reporter(wz::Logger& logger)
    {
        // last_dropped persists across cadence calls (the lane never copies the
        // reporter between calls), so the table-cap note logs only when it grows.
        return [&logger, last_dropped = uint64_t{0}](
                   DiagnosticAggregate& state) mutable {
            for (std::size_t i = 0; i < state.size(); ++i) {
                const auto& e = state.entry(i);
                if (e.unreported() == 0) {
                    continue;  // nothing new since the last report -- no line
                }
                std::string line = source_label(e.source);
                line += " diagnostic #";
                line += std::to_string(e.id);
                line += " x";
                line += std::to_string(e.count);      // the EXACT running total (#291)
                line += " [pass ";
                line += std::to_string(e.last_pass);
                line += "]";
                emit(logger, e.severity, line);
                state.mark_reported(i);
            }
            if (state.dropped() > last_dropped) {
                logger.warn(
                    "diagnostics: table full, "
                    + std::to_string(state.dropped())
                    + " occurrences of untracked ids not itemized");
                last_dropped = state.dropped();
            }
        };
    }

} // namespace wz::diag
