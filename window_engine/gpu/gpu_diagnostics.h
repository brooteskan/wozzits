#pragma once

// gpu/gpu_diagnostics.h
//
// The backend-agnostic accessor for the GPU debug layer's diagnostic reporter
// (#291 / #305 step 4d). The LoggerService lane formats debug-layer Diagnostic
// records through this; the concrete formatter (category names, the main/offscreen
// pass label) lives in the active backend (dx12 today). Keeps the runtime's lane
// install site depending on the abstract gpu API, not a backend's internals.

#include <diagnostics/logger_service.h>  // DiagnosticReporter

namespace wz { struct Logger; }

namespace wz::gpu
{
    // Build a DiagnosticReporter for the GPU debug layer's records: it names the
    // message category and the main/offscreen pass, keeps the exact repeat count,
    // and logs through `logger`, which must outlive the lane's reporting (the
    // runtime quiesces the lane before shutting the logger down). Resolved to the
    // linked backend's implementation.
    wz::diag::DiagnosticReporter make_debug_layer_reporter(wz::Logger& logger);
}
