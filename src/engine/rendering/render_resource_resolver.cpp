// src/engine/rendering/render_resource_resolver.cpp

#include <engine/rendering/render_resource_resolver.h>

namespace wz::engine::rendering
{
    wz::scene::SplatHandle RenderResourceResolver::register_splat_cloud(
        wz::gpu::GPUHandle gpu_handle)
    {
        const auto index =
            static_cast<wz::scene::SplatHandle>(splat_entries_.size());
        splat_entries_.push_back(gpu_handle);
        return index;
    }

    wz::gpu::GPUHandle RenderResourceResolver::resolve_splats(
        wz::scene::SplatHandle handle) const noexcept
    {
        if (handle == wz::scene::INVALID_SPLAT)
            return {};
        if (static_cast<size_t>(handle) >= splat_entries_.size())
            return {};
        return splat_entries_[handle];
    }
}
