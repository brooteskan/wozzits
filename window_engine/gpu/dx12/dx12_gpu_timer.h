#pragma once
// gpu/dx12/dx12_gpu_timer.h
//
// Lightweight GPU timestamp query wrapper for DX12.
// Create once, then bracket GPU work with begin/end per slot each frame.
// After end_frame's fence wait, call resolve() then read results with elapsed_ms().

#include <cstdint>

namespace wz::gpu
{
    struct Device;
}

namespace wz::gpu::dx12
{
    // Opaque handle — forward-declared, allocated internally.
    struct GpuTimerImpl;

    struct GpuTimer
    {
        GpuTimerImpl* impl = nullptr;

        bool valid() const noexcept { return impl != nullptr; }
    };

    // Create a timer with `slot_count` independent measurement slots.
    // Each slot holds a begin+end timestamp pair.
    GpuTimer create_gpu_timer(Device& device, uint32_t slot_count);

    void destroy_gpu_timer(GpuTimer& timer);

    // Insert a begin timestamp for `slot` into the current command list.
    void gpu_timer_begin(GpuTimer& timer, Device& device, uint32_t slot);

    // Insert an end timestamp for `slot` into the current command list.
    void gpu_timer_end(GpuTimer& timer, Device& device, uint32_t slot);

    // Resolve all timestamps from GPU query heap into the readback buffer.
    // Call this after the command list has been submitted and the GPU fence
    // has been signalled (i.e. after end_frame).
    void gpu_timer_resolve(GpuTimer& timer, Device& device);

    // Read the elapsed time for `slot` in milliseconds.
    // Returns 0.0 if the slot hasn't been resolved yet.
    double gpu_timer_elapsed_ms(const GpuTimer& timer, uint32_t slot);
}
