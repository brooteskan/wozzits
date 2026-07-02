#include <engine/rendering/rhi_gpu_backend.h>

#include <gpu/dx12/dx12_internal.h>

namespace wz::engine::rendering
{
    wz::rhi::BackendResource EngineGpuBackend::create(
        const wz::rhi::GpuResourceDesc& desc)
    {
        wz::gpu::GPUHandle handle{};

        if (desc.dimension == wz::rhi::ResourceDimension::Buffer) {
            const wz::gpu::ComputeBufferDesc buffer_desc =
                to_compute_buffer_desc(desc);
            if (!buffer_desc.valid()) {
                return {};
            }
            handle = desc_wants_rw(desc)
                ? wz::gpu::create_rw_structured_buffer(*device_, buffer_desc)
                : wz::gpu::create_structured_buffer(*device_, buffer_desc);
        }
        else {
            // Texture2D / Texture3D -> the generic engine texture creator.
            wz::gpu::TextureDesc texture_desc{};
            if (!to_texture_desc(desc, texture_desc)) {
                return {};
            }
            handle = wz::gpu::create_texture(*device_, texture_desc);
        }

        if (!handle.valid()) {
            return {};
        }

        const uint64_t token = next_token_++;
        resources_.emplace(token, Entry{ handle, desc.dimension });
        return wz::rhi::BackendResource{ token };
    }

    void EngineGpuBackend::destroy(wz::rhi::BackendResource resource)
    {
        const auto it = resources_.find(resource.id);
        if (it == resources_.end()) {
            return;
        }
        if (it->second.dimension == wz::rhi::ResourceDimension::Buffer) {
            wz::gpu::release_compute_buffer(*device_, it->second.handle);
        }
        else {
            wz::gpu::release_texture(*device_, it->second.handle);
        }
        resources_.erase(it);
    }

    bool EngineGpuBackend::write(
        wz::rhi::BackendResource resource,
        const void* data,
        uint64_t size,
        uint64_t offset)
    {
        const auto it = resources_.find(resource.id);
        if (it == resources_.end() || !it->second.handle.valid()) {
            return false;
        }
        if (it->second.dimension == wz::rhi::ResourceDimension::Buffer) {
            return wz::gpu::dx12::internal::update_compute_buffer_dx12(
                *device_,
                it->second.handle,
                data,
                size,
                offset);
        }
        return wz::gpu::update_texture(
            *device_,
            it->second.handle,
            data,
            size,
            offset);
    }

    bool EngineGpuBackend::write_texture_mip(
        wz::rhi::BackendResource resource,
        uint32_t mip_level,
        const void* data,
        uint64_t size)
    {
        const auto it = resources_.find(resource.id);
        if (it == resources_.end() || !it->second.handle.valid()) {
            return false;
        }
        // Textures only; a buffer resource has no mip concept.
        if (it->second.dimension == wz::rhi::ResourceDimension::Buffer) {
            return false;
        }
        return wz::gpu::update_texture_mip(
            *device_,
            it->second.handle,
            mip_level,
            data,
            size);
    }

    wz::gpu::GPUHandle EngineGpuBackend::gpu_handle_for(
        wz::rhi::BackendResource resource) const
    {
        const auto it = resources_.find(resource.id);
        return it != resources_.end() ? it->second.handle : wz::gpu::GPUHandle{};
    }
}
