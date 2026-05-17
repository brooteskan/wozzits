// src/engine/rendering/render_resource_resolver.cpp

#include <engine/rendering/render_resource_resolver.h>

namespace wz::engine::rendering
{
    wz::scene::SplatHandle RenderResourceResolver::register_splat_cloud(
        wz::gpu::GPUHandle                       gpu_resource,
        wz::engine::assets::BuiltinRenderProgram program)
    {
        const auto index =
            static_cast<wz::scene::SplatHandle>(splat_entries_.size());
        splat_entries_.push_back({ gpu_resource, program });
        return index;
    }

    std::optional<ResolvedRenderableResource>
    RenderResourceResolver::resolve_splats(
        wz::scene::SplatHandle handle) const noexcept
    {
        if (handle == wz::scene::INVALID_SPLAT)
            return std::nullopt;
        if (static_cast<size_t>(handle) >= splat_entries_.size())
            return std::nullopt;
        const Entry& e = splat_entries_[handle];
        return ResolvedRenderableResource{ e.gpu_resource, e.program };
    }

    wz::scene::MeshHandle RenderResourceResolver::register_mesh(
        wz::gpu::GPUHandle                       gpu_resource,
        wz::engine::assets::BuiltinRenderProgram program)
    {
        const auto index =
            static_cast<wz::scene::MeshHandle>(mesh_entries_.size());
        mesh_entries_.push_back({ gpu_resource, program });
        return index;
    }

    std::optional<ResolvedRenderableResource>
    RenderResourceResolver::resolve_mesh(
        wz::scene::MeshHandle handle) const noexcept
    {
        if (handle == wz::scene::INVALID_MESH)
            return std::nullopt;
        if (static_cast<size_t>(handle) >= mesh_entries_.size())
            return std::nullopt;
        const Entry& e = mesh_entries_[handle];
        return ResolvedRenderableResource{ e.gpu_resource, e.program };
    }
}
