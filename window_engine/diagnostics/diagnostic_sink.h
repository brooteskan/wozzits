#pragma once

// diagnostics/diagnostic_sink.h
//
// The publish seam a hot lane sees (#291 / #305 step 4d), mirroring the
// IAsyncExecutor / set_async_executor pattern: a global install point plus a
// narrow interface, so a producer (the D3D12 InfoQueue drain on the GPUOwner lane
// first) depends only on "publish a record", never on the LoggerService lane's
// concrete type. Inert until a lane is installed -- get_diagnostic_sink() returns
// nullptr and the producer simply skips publishing (like async_* returning IOError
// with no executor), so detection code carries no #ifdef.

#include "diagnostic_record.h"

namespace wz::diag
{
    struct IDiagnosticSink
    {
        virtual ~IDiagnosticSink() = default;

        // Publish one detected event. Must return quickly and never block or
        // allocate -- it is called from a hot lane. A full channel drops the record
        // (the occurrence is delayed/lost, never a wrong counted total).
        virtual void publish(const DiagnosticRecord& record) = 0;
    };

    // Install the global diagnostic sink (the LoggerService lane). The caller
    // retains ownership; the sink must outlive every producer that can publish.
    // Typically installed once at engine init and cleared at teardown.
    void set_diagnostic_sink(IDiagnosticSink* sink);

    // The installed sink, or nullptr if none. Producers null-check and skip.
    IDiagnosticSink* get_diagnostic_sink();

} // namespace wz::diag
