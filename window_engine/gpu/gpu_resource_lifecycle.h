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
