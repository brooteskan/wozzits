#pragma once
// gpu/gpu_resource_lifecycle.h
//
// Type-dispatched GPU resource release and deferred release queue.
//
// release_gpu_resource() inspects GPUHandle::type and calls the matching
// backend release function (release_mesh, release_gaussian_splat_cloud, …).
// Adding a new GPU resource type means adding one case here — callers never
// need to know which release function to use.
//
// DeferredReleaseQueue delays destruction by N frames so that resources
// still referenced by in-flight command lists are not freed prematurely.
// Call defer_release() instead of immediate release; call advance_frame()
// once per frame from the render loop.

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>

#include <array>
#include <vector>

namespace wz::gpu
{
    // Immediately release a GPU resource, dispatching by handle type.
    // Returns false if the handle is invalid or the type is unrecognized.
    bool release_gpu_resource(Device& device, GPUHandle handle);

    // Frame-based deferred release queue.
    //
    // Resources pushed via defer_release() are held for kFrameLatency frames
    // before being released, ensuring the GPU has finished reading them.
    //
    // THE LATENCY IS COUPLED TO THE BACKEND'S FRAME RING and nothing here can
    // see it. A resource deferred during frame N is released when advance_frame
    // wraps back to its slot, i.e. kFrameLatency frames later; the GPU is done
    // with frame N only once the CPU is more than frames_in_flight frames past
    // it. So the queue is safe exactly while
    //
    //     kFrameLatency > frames_in_flight
    //
    // 3 > 2 holds for the N=2 ring shipped today, with ONE frame of margin --
    // raising the ring to 3 without raising this makes the queue release
    // resources the GPU is still reading, silently and only under load. The
    // relationship is asserted where both constants are visible (the DX12
    // backend's dx12_device.cpp); this comment is the reason it exists.
    class DeferredReleaseQueue
    {
    public:
        static constexpr uint32_t kFrameLatency = 3;

        // Queue a handle for deferred release.
        void defer_release(GPUHandle handle);

        // Advance to the next frame and release resources that are now safe.
        // Call this once per frame, e.g. at the start of begin_frame().
        void advance_frame(Device& device);

        // Immediately release all pending resources (e.g. on shutdown).
        void flush(Device& device);

    private:
        std::array<std::vector<GPUHandle>, kFrameLatency> pending_{};
        uint32_t current_frame_ = 0;
    };
}
