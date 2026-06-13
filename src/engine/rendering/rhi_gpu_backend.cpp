#include <engine/rendering/rhi_gpu_backend.h>

namespace wz::engine::rendering
{
    wz::rhi::BackendResource EngineGpuBackend::create(
        const wz::rhi::GpuResourceDesc& desc)
    {
        // TODO(rhi-backend): texture / render-target creation needs a generic
        // engine texture creator (not yet exposed). Buffers only for now.
        if (desc.dimension != wz::rhi::ResourceDimension::Buffer) {
            return {};
        }

        const wz::gpu::ComputeBufferDesc buffer_desc =
            to_compute_buffer_desc(desc);
        if (!buffer_desc.valid()) {
            return {};
        }

        const wz::gpu::GPUHandle handle = desc_wants_rw(desc)
            ? wz::gpu::create_rw_structured_buffer(*device_, buffer_desc)
            : wz::gpu::create_structured_buffer(*device_, buffer_desc);
        if (!handle.valid()) {
            return {};
        }

        const uint64_t token = next_token_++;
        resources_.emplace(token, handle);
        return wz::rhi::BackendResource{ token };
    }

    void EngineGpuBackend::destroy(wz::rhi::BackendResource resource)
    {
        const auto it = resources_.find(resource.id);
        if (it == resources_.end()) {
            return;
        }
        wz::gpu::release_compute_buffer(*device_, it->second);
        resources_.erase(it);
    }

    bool EngineGpuBackend::write(
        wz::rhi::BackendResource /*resource*/,
        const void* /*data*/,
        uint64_t /*size*/,
        uint64_t /*offset*/)
    {
        // TODO(rhi-backend): the engine has no generic CPU-write-to-existing-
        // buffer primitive yet (compute buffers take initial_data at creation,
        // and the #145 mutable path uses a GPU-source copy). Wire CPU writes /
        // the in-place update path in a later increment.
        return false;
    }
}
