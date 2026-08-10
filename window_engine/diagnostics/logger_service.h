#pragma once

// diagnostics/logger_service.h
//
// The LoggerService lane (#291 / #305 step 4d): a dedicated COLD thread that turns
// engine-owned DiagnosticState into log lines, off the hot lanes that detect the
// events. It is ExecutionLane::LoggerService made real -- the enumerator existed
// (window_engine/jobs/job_types.h) but nothing drove it.
//
// Shape mirrors the IO lane (window_engine/io/io_executor.h): a concrete class
// implementing a narrow seam (IDiagnosticSink::publish), owning its own thread,
// installed once via set_diagnostic_sink. Its OWN thread, not the compute pool or
// the logger's worker: reporting runs on a slow cadence and calls the logger (more
// I/O), which is neither the compute pool's core-bound work-stealing shape nor the
// logger worker's latency-drain shape.
//
// The producer (a hot lane) publish()es POD records; the lane drains them into a
// DiagnosticState on its cadence and hands the aggregate to a reporter that formats
// + logs. Detection is cheap ints on the hot lane; string-building and logger I/O
// live entirely here.
//
// Teardown ordering (why quiesce() exists): the reporter calls a wz::Logger, whose
// state pointer is torn down by shutdown_logger() -- and reading it while that runs
// is a data race. But the AMD-driver rule wants lane threads joined AFTER
// destroy_device. So the runtime calls quiesce() BEFORE engine teardown (final
// report while the logger lives, then the lane stops touching it), and destroys the
// lane AFTER -- the thread is parked and logger-silent across shutdown, joined once
// the device is gone.
//
// The runtime now ALSO splits the logger out of engine teardown
// (engine::shutdown_subsystems() ... join lanes ... engine::shutdown_logging(), see
// engine/app_context.h), so the logger outlives every lane thread and the window
// quiesce() covers is no longer the only thing standing between the reporter and a
// freed logger. quiesce() still earns its place: it pins the FINAL report to a
// defined point, before the GPU it reports on is destroyed.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>

#include "diagnostic_channel.h"
#include "diagnostic_sink.h"
#include "diagnostic_state.h"

namespace wz { struct Logger; }  // make_logging_reporter's sink; full type in the .cpp

namespace wz::diag
{
    // The aggregate the lane owns. 64 keys matches the D3D12 tally cap this
    // replaces; distinct ids past it are dropped and counted (DiagnosticState).
    inline constexpr std::size_t kLoggerServiceStateCapacity = 64;
    using DiagnosticAggregate = DiagnosticState<kLoggerServiceStateCapacity>;

    // Called on the lane thread each cadence with the freshly-drained aggregate.
    // Logs entries with unreported() occurrences and marks them reported (so a
    // steady repeat is logged once per cadence with its true total, not per frame).
    // Runs only while reporting is enabled -- never during/after quiesce().
    using DiagnosticReporter = std::function<void(DiagnosticAggregate&)>;

    class LoggerServiceLane final : public IDiagnosticSink
    {
    public:
        // report_interval is the cadence the lane wakes on to drain + report.
        explicit LoggerServiceLane(
            DiagnosticReporter reporter,
            std::chrono::milliseconds report_interval =
                std::chrono::milliseconds(250));

        // Stops the thread and drains once more before joining -- a record
        // published just before teardown is still folded in (reported only if the
        // lane was not already quiesced). See quiesce() for the logger-lifetime
        // ordering the runtime must honour.
        ~LoggerServiceLane() override;

        LoggerServiceLane(const LoggerServiceLane&)            = delete;
        LoggerServiceLane& operator=(const LoggerServiceLane&) = delete;

        // Producer seam (any thread): publish a detected event. Non-blocking, never
        // allocates; a full channel drops the record. No wake -- the cadence (or a
        // flush_now) drains, so a hot lane never touches the lane's cv.
        void publish(const DiagnosticRecord& record) override;

        // Drain + report NOW, blocking until the lane thread has completed one
        // drain+report cycle. A checkpoint flush, and the deterministic hook tests
        // use instead of racing the cadence.
        void flush_now();

        // Teardown step, called while the logger is still alive and BEFORE engine
        // shutdown: do a final flush_now(), then stop reporting. After this the lane
        // thread still drains (discarding) but never calls the reporter, so the
        // logger can be shut down before this lane's thread is joined. Idempotent.
        void quiesce();

    private:
        void worker_main();
        void drain_and_report();  // lane thread only: channel_ -> state_ -> reporter_

        DiagnosticChannel   channel_;
        DiagnosticAggregate state_;
        DiagnosticReporter  reporter_;
        std::chrono::milliseconds interval_;

        std::mutex              mutex_;
        std::condition_variable wake_cv_;     // worker waits: cadence / flush / stop
        std::condition_variable flushed_cv_;  // flush_now waits for completion
        bool                    running_  = true;
        std::atomic<bool>       reporting_{true};  // cleared by quiesce()
        uint64_t                flush_request_ = 0;
        uint64_t                flush_done_    = 0;
        std::thread             thread_;
    };

    // The report cycle a DiagnosticReporter runs, shared by the generic reporter
    // and backend-specific ones (e.g. the D3D12 debug-layer reporter, which formats
    // the same entries with category names and a main/offscreen label): log every
    // entry with new occurrences -- its text produced by `format_line` -- at the
    // entry's severity, mark it reported, then note any growth in the table-cap drop
    // count. `last_dropped` is the reporter's carried state (drops reported last
    // cycle). Runs on the lane thread.
    void report_diagnostics(
        DiagnosticAggregate& state,
        wz::Logger& logger,
        const std::function<std::string(const DiagnosticAggregate::Entry&)>&
            format_line,
        uint64_t& last_dropped);

    // A generic reporter that logs each unreported entry through `logger` as
    // "<source> diagnostic #<id> x<count> [pass <n>]" at the entry's severity, and
    // notes any table-cap drops. The GPU debug-layer client supplies a richer one
    // (wz::gpu::make_debug_layer_reporter); this is the default. `logger` must
    // outlive the lane's reporting (the runtime quiesces before shutting it down).
    DiagnosticReporter make_logging_reporter(wz::Logger& logger);

} // namespace wz::diag
