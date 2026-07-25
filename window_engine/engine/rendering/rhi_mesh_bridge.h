#pragma once

#include <wozzits/rhi/gpu_resource_registry.h>

#include <cstdint>

namespace wz::engine::rendering
{
    [[nodiscard]] inline wz::rhi::GpuResourceHandle acquire_pull_buffer(
        wz::rhi::GpuResourceRegistry& resources,
        uint64_t asset_id,
        wz::rhi::Tag variant,
        const void* data,
        uint64_t size,
        uint32_t stride,
        wz::rhi::ResourceCpuAccess cpu_access =
            wz::rhi::ResourceCpuAccess::WriteOnce)
    {
        // cpu_access defaults to WriteOnce (static pull buffers); pass WriteFrequent
        // for a buffer the renderer re-uploads each frame (e.g. puppet deform).
        wz::rhi::GpuResourceDesc desc = wz::rhi::GpuResourceDesc::buffer(
            size,
            stride,
            wz::rhi::ResourceUsage_Sampled,
            cpu_access);
        desc.identity = wz::rhi::ResourceIdentity{ asset_id, variant };

        const wz::rhi::GpuResourceHandle handle =
            resources.acquire(desc);
        if (handle.valid()) {
            resources.update(handle, data, size);
        }
        return handle;
    }
}
