// src/gpu/gpu_resource_lifecycle.cpp

#include <gpu/gpu_resource_lifecycle.h>

#include <gpu/mesh.h>
#include <gpu/gaussian_splat.h>
#include <asset/types.h>

namespace wz::gpu
{
    bool release_gpu_resource(Device& device, GPUHandle handle)
    {
        if (!device.valid() || !handle.valid())
            return false;

        switch (handle.type) {
        case GPUResourceType::Mesh:
            return release_mesh(device, handle);

        case GPUResourceType::GaussianSplatCloud:
            return release_gaussian_splat_cloud(device, handle);

        default:
            return false;
        }
    }

    void DeferredReleaseQueue::defer_release(GPUHandle handle)
    {
        if (!handle.valid())
            return;

        pending_[current_frame_].push_back(handle);
    }

    void DeferredReleaseQueue::advance_frame(Device& device)
    {
        // Move to the next slot. Resources stored there are from
        // kFrameLatency frames ago — guaranteed not in flight.
        current_frame_ = (current_frame_ + 1) % kFrameLatency;

        for (const GPUHandle& h : pending_[current_frame_]) {
            release_gpu_resource(device, h);
        }
        pending_[current_frame_].clear();
    }

    void DeferredReleaseQueue::flush(Device& device)
    {
        for (auto& bucket : pending_) {
            for (const GPUHandle& h : bucket) {
                release_gpu_resource(device, h);
            }
            bucket.clear();
        }
    }
}
