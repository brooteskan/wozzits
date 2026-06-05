// src/engine/rendering/render_resource_resolver.cpp

#include <engine/rendering/render_resource_resolver.h>

namespace wz::engine::rendering
{
    wz::scene::SplatHandle RenderResourceResolver::register_splat_cloud(
        wz::gpu::GPUHandle                       gpu_resource,
        wz::engine::assets::BuiltinRenderProgram program,
        wz::asset::ResourceHandle                render_program)
    {
        const auto index =
            static_cast<wz::scene::SplatHandle>(splat_entries_.size());
        splat_entries_.push_back({ gpu_resource, program, render_program });
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
        return ResolvedRenderableResource{
            e.gpu_resource,
            e.program,
            e.render_program,
            e.terrain_lighting,
            e.mesh_style };
    }

    bool RenderResourceResolver::set_splat_render_program(
        wz::scene::SplatHandle    handle,
        wz::asset::ResourceHandle render_program) noexcept
    {
        if (handle == wz::scene::INVALID_SPLAT)
            return false;
        if (static_cast<size_t>(handle) >= splat_entries_.size())
            return false;
        splat_entries_[handle].render_program = render_program;
        return true;
    }

    bool RenderResourceResolver::set_splat_gpu_resource(
        wz::scene::SplatHandle handle,
        wz::gpu::GPUHandle     gpu_resource) noexcept
    {
        if (handle == wz::scene::INVALID_SPLAT)
            return false;
        if (static_cast<size_t>(handle) >= splat_entries_.size())
            return false;
        splat_entries_[handle].gpu_resource = gpu_resource;
        return true;
    }

    wz::scene::MeshHandle RenderResourceResolver::register_mesh(
        wz::gpu::GPUHandle                       gpu_resource,
        wz::engine::assets::BuiltinRenderProgram program,
        wz::asset::ResourceHandle                render_program,
        wz::engine::assets::TerrainLightingData  terrain_lighting,
        wz::engine::assets::MeshRenderStyleData  mesh_style,
        std::span<const wz::engine::assets::TerrainVisualChunk> terrain_chunks)
    {
        const auto index =
            static_cast<wz::scene::MeshHandle>(mesh_entries_.size());
        Entry entry{};
        entry.gpu_resource = gpu_resource;
        entry.program = program;
        entry.render_program = render_program;
        entry.terrain_lighting = terrain_lighting;
        entry.mesh_style = mesh_style;
        entry.terrain_chunks.assign(
            terrain_chunks.begin(),
            terrain_chunks.end());
        mesh_entries_.push_back(std::move(entry));
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
        return ResolvedRenderableResource{
            e.gpu_resource,
            e.program,
            e.render_program,
            e.terrain_lighting,
            e.mesh_style,
            e.terrain_chunks };
    }

    void RenderResourceResolver::reset_terrain_render_stats() const noexcept
    {
        terrain_stats_ = {};
    }

    void RenderResourceResolver::record_terrain_render_stats(
        uint64_t total_chunks,
        uint64_t submitted_chunks,
        uint64_t total_triangles,
        uint64_t submitted_triangles) const noexcept
    {
        terrain_stats_.total_chunks += total_chunks;
        terrain_stats_.submitted_chunks += submitted_chunks;
        terrain_stats_.total_triangles += total_triangles;
        terrain_stats_.submitted_triangles += submitted_triangles;
    }

    TerrainRenderStats RenderResourceResolver::terrain_render_stats()
        const noexcept
    {
        return terrain_stats_;
    }
}
