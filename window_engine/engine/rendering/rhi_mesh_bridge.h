#pragma once

#include <engine/rendering/rhi_context.h>

#include <cstdint>

namespace wz::engine::rendering
{
    [[nodiscard]] inline wz::rhi::GpuResourceHandle acquire_pull_buffer(
        RhiContext& ctx,
        uint64_t asset_id,
        wz::rhi::Tag variant,
        const void* data,
        uint64_t size,
        uint32_t stride)
    {
        wz::rhi::GpuResourceDesc desc = wz::rhi::GpuResourceDesc::buffer(
            size,
            stride,
            wz::rhi::ResourceUsage_Sampled,
            wz::rhi::ResourceCpuAccess::WriteOnce);
        desc.identity = wz::rhi::ResourceIdentity{ asset_id, variant };

        const wz::rhi::GpuResourceHandle handle =
            ctx.resources.acquire(desc);
        if (handle.valid()) {
            ctx.resources.update(handle, data, size);
        }
        return handle;
    }
}
