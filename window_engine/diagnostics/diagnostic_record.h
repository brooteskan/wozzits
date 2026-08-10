#pragma once

// diagnostics/diagnostic_record.h
//
// The unit of engine-owned diagnostic STATE that crosses a lane boundary (#291 /
// #305 step 4d). A producer on a hot lane -- the GPUOwner lane draining the D3D12
// InfoQueue is the first client -- detects an event and publishes a POD record;
// the cold LoggerService lane turns records into log lines on its own cadence.
//
// The point of the split (#291): detection is a cheap flag on the hot lane;
// REPORTING -- building the string, calling the logger -- is another lane's job.
// So a record carries only ints/enums: NO string, NO allocation. The id is
// opaque (source-defined, e.g. a D3D12_MESSAGE_ID); resolving it to a human name
// is the consumer/formatter's job, off the hot lane.
//
// Deliberately engine-level and D3D12-agnostic: "Engine-detected render/GPU
// errors use the same channel" (#291), so nothing here depends on dx12. The
// record is a trivially-copyable POD because it rides the same kind of lock-free
// ring the logger and audio scheduler use, which move values by assignment.

#include <cstdint>
#include <type_traits>

namespace wz::diag
{
    // Severity, collapsed to the engine's view. A source maps its own scale onto
    // this (D3D12 CORRUPTION/ERROR/WARNING/INFO/MESSAGE -> Corruption/Error/
    // Warning/Info, folding INFO+MESSAGE into Info). Ordered by increasing
    // seriousness so a consumer can threshold on it.
    enum class DiagnosticSeverity : uint8_t
    {
        Info = 0,
        Warning,
        Error,
        Corruption,
    };

    // Which subsystem detected the event. Part of the record so one channel can
    // carry more than the InfoQueue (the first client) without id collisions --
    // an engine-detected render error and a D3D12 message may share a numeric id
    // but are distinct keys. Extend as new producers land.
    enum class DiagnosticSource : uint8_t
    {
        Engine = 0,
        D3D12,
    };

    struct DiagnosticRecord
    {
        // Opaque, source-defined identity (e.g. a D3D12_MESSAGE_ID). Keyed on with
        // `source` by the consumer's aggregate; the consumer/formatter maps it to a
        // name. Never a string here -- that would be hot-lane allocation.
        uint32_t id = 0;

        // How many times the producer saw this id in the single drain that emitted
        // this record. Lets a producer dedup within one frame (push one record per
        // distinct id) while the consumer still accumulates the EXACT total -- the
        // count the deny-after-N filter used to throw away (#291).
        uint32_t occurrences = 1;

        DiagnosticSeverity severity = DiagnosticSeverity::Info;
        DiagnosticSource   source   = DiagnosticSource::Engine;

        // Opaque, source-defined category (e.g. a D3D12_MESSAGE_CATEGORY).
        uint8_t category = 0;

        // Source-defined pass attribution, captured AT THE SOURCE (#291): the
        // producer stamps the pass it is already in so the consumer never has to
        // reconstruct it. The formatter maps the number to a label (e.g. main /
        // offscreen). 0 is "unspecified".
        uint16_t pass = 0;
    };

    // Rides a lock-free ring buffer (move-by-assignment), same as LogMessage and
    // AudioCommand. Keep it a trivial POD.
    static_assert(
        std::is_trivially_copyable_v<DiagnosticRecord>,
        "DiagnosticRecord crosses a lock-free ring by value; keep it trivially "
        "copyable (no string/owned resource -- that would be hot-lane allocation).");

} // namespace wz::diag
