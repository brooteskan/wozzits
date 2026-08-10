#pragma once

// diagnostics/diagnostic_channel.h
//
// The lock-free cross-lane channel diagnostics ride (#291 / #305 step 4d): a
// producer on a hot lane (the GPUOwner lane draining the D3D12 InfoQueue) pushes
// DiagnosticRecords; the cold LoggerService lane is the single consumer, draining
// them into a DiagnosticState on its own cadence.
//
// It is the SAME primitive the logger uses (MPSCRingBuffer) -- "the same lock-free
// producer->consumer channel already used for logging (MPSC) and audio (SPSC)"
// (#291). MPSC (not SPSC) because more than one producing lane may publish (the
// InfoQueue first, engine-detected GPU errors next), and try_push never blocks or
// allocates -- a full ring drops, exactly the back-pressure a hot lane needs.
//
// Capacity holds the burst between consumer drains: the producer emits at most one
// record per distinct id per frame (the id-dedup happens at the source, the exact
// count rides in DiagnosticRecord::occurrences), the D3D12 tally caps that at 64
// distinct ids/frame, so this covers ~32 frames of worst-case distinct ids before
// the LoggerService lane wakes -- ample for a sub-second cadence, and a drop only
// costs a delayed occurrence, never a wrong total once counted.

#include <cstddef>

#include <containers/mpsc_ring_buffer.h>

#include "diagnostic_record.h"

namespace wz::diag
{
    inline constexpr std::size_t kDiagnosticChannelCapacity = 2048;

    using DiagnosticChannel =
        wz::core::containers::MPSCRingBuffer<DiagnosticRecord,
                                             kDiagnosticChannelCapacity>;

} // namespace wz::diag
