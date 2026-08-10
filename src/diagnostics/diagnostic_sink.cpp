#include <diagnostics/diagnostic_sink.h>

#include <atomic>

// The global diagnostic-sink seam (#291 / #305 step 4d). One atomic pointer, set
// once at engine init and cleared at teardown, read by producers on hot lanes.
// Same shape as src/tasks/task_scheduler.cpp's g_scheduler and src/async/async.cpp.

namespace wz::diag
{
    namespace
    {
        std::atomic<IDiagnosticSink*> g_sink{nullptr};
    }

    void set_diagnostic_sink(IDiagnosticSink* sink)
    {
        g_sink.store(sink, std::memory_order_release);
    }

    IDiagnosticSink* get_diagnostic_sink()
    {
        return g_sink.load(std::memory_order_acquire);
    }
}
