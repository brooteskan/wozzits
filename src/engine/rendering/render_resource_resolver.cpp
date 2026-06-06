// src/engine/rendering/render_resource_resolver.cpp

#include <engine/rendering/render_resource_resolver.h>

#include <algorithm>

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
            e.terrain_target_pixels_per_triangle,
            e.mesh_style,
            e.terrain_chunks,
            e.terrain_far_splat_chunks };
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
        float                                   terrain_target_pixels_per_triangle,
        wz::engine::assets::MeshRenderStyleData  mesh_style,
        std::span<const wz::engine::assets::TerrainVisualChunk> terrain_chunks,
        std::span<const TerrainFarSplatChunk> terrain_far_splat_chunks)
    {
        const auto index =
            static_cast<wz::scene::MeshHandle>(mesh_entries_.size());
        Entry entry{};
        entry.gpu_resource = gpu_resource;
        entry.program = program;
        entry.render_program = render_program;
        entry.terrain_lighting = terrain_lighting;
        entry.terrain_target_pixels_per_triangle =
            terrain_target_pixels_per_triangle;
        entry.mesh_style = mesh_style;
        entry.terrain_chunks.assign(
            terrain_chunks.begin(),
            terrain_chunks.end());
        entry.terrain_far_splat_chunks.assign(
            terrain_far_splat_chunks.begin(),
            terrain_far_splat_chunks.end());
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
            e.terrain_target_pixels_per_triangle,
            e.mesh_style,
            e.terrain_chunks,
            e.terrain_far_splat_chunks };
    }

    void RenderResourceResolver::reset_terrain_render_stats() const noexcept
    {
        terrain_stats_ = {};
    }

    void RenderResourceResolver::record_terrain_render_stats(
        uint64_t total_chunks,
        uint64_t submitted_chunks,
        uint64_t total_triangles,
        uint64_t submitted_triangles,
        uint64_t lod_candidate_chunks,
        uint64_t lod_candidate_triangles,
        uint64_t lod_replacement_available_chunks,
        uint64_t lod_replacement_available_triangles,
        uint64_t lod_replacement_drawn_chunks,
        uint64_t lod_replacement_drawn_triangles,
        uint64_t far_splat_chunks,
        uint64_t far_splats,
        float lod_target_pixels_per_triangle,
        float pixels_per_triangle_min,
        float pixels_per_triangle_max,
        double pixels_per_triangle_weighted_sum,
        uint64_t pixels_per_triangle_triangles_le_0_5,
        uint64_t pixels_per_triangle_triangles_le_1,
        uint64_t pixels_per_triangle_triangles_le_2,
        uint64_t pixels_per_triangle_triangles_le_4,
        uint64_t pixels_per_triangle_triangles_le_8,
        uint64_t pixels_per_triangle_triangles_le_16,
        uint64_t pixels_per_triangle_triangles_le_32,
        uint64_t pixels_per_triangle_triangles_le_64,
        uint64_t pixels_per_triangle_triangles_le_128,
        uint64_t pixels_per_triangle_triangles_le_256) const noexcept
    {
        const uint64_t previous_submitted_triangles =
            terrain_stats_.submitted_triangles;
        const bool had_pixels_per_triangle =
            previous_submitted_triangles > 0
            && terrain_stats_.pixels_per_triangle_max
                >= terrain_stats_.pixels_per_triangle_min;
        const double previous_weighted_sum =
            static_cast<double>(
                terrain_stats_.pixels_per_triangle_weighted_mean)
            * static_cast<double>(previous_submitted_triangles);

        terrain_stats_.total_chunks += total_chunks;
        terrain_stats_.submitted_chunks += submitted_chunks;
        terrain_stats_.total_triangles += total_triangles;
        terrain_stats_.submitted_triangles += submitted_triangles;
        terrain_stats_.lod_candidate_chunks += lod_candidate_chunks;
        terrain_stats_.lod_candidate_triangles += lod_candidate_triangles;
        terrain_stats_.lod_replacement_available_chunks +=
            lod_replacement_available_chunks;
        terrain_stats_.lod_replacement_available_triangles +=
            lod_replacement_available_triangles;
        terrain_stats_.lod_replacement_drawn_chunks +=
            lod_replacement_drawn_chunks;
        terrain_stats_.lod_replacement_drawn_triangles +=
            lod_replacement_drawn_triangles;
        terrain_stats_.far_splat_chunks += far_splat_chunks;
        terrain_stats_.far_splats += far_splats;
        terrain_stats_.lod_target_pixels_per_triangle = (std::max)(
            terrain_stats_.lod_target_pixels_per_triangle,
            lod_target_pixels_per_triangle);
        terrain_stats_.pixels_per_triangle_triangles_le_0_5 +=
            pixels_per_triangle_triangles_le_0_5;
        terrain_stats_.pixels_per_triangle_triangles_le_1 +=
            pixels_per_triangle_triangles_le_1;
        terrain_stats_.pixels_per_triangle_triangles_le_2 +=
            pixels_per_triangle_triangles_le_2;
        terrain_stats_.pixels_per_triangle_triangles_le_4 +=
            pixels_per_triangle_triangles_le_4;
        terrain_stats_.pixels_per_triangle_triangles_le_8 +=
            pixels_per_triangle_triangles_le_8;
        terrain_stats_.pixels_per_triangle_triangles_le_16 +=
            pixels_per_triangle_triangles_le_16;
        terrain_stats_.pixels_per_triangle_triangles_le_32 +=
            pixels_per_triangle_triangles_le_32;
        terrain_stats_.pixels_per_triangle_triangles_le_64 +=
            pixels_per_triangle_triangles_le_64;
        terrain_stats_.pixels_per_triangle_triangles_le_128 +=
            pixels_per_triangle_triangles_le_128;
        terrain_stats_.pixels_per_triangle_triangles_le_256 +=
            pixels_per_triangle_triangles_le_256;

        if (submitted_triangles > 0) {
            if (!had_pixels_per_triangle) {
                terrain_stats_.pixels_per_triangle_min =
                    pixels_per_triangle_min;
                terrain_stats_.pixels_per_triangle_max =
                    pixels_per_triangle_max;
            }
            else {
                terrain_stats_.pixels_per_triangle_min = (std::min)(
                    terrain_stats_.pixels_per_triangle_min,
                    pixels_per_triangle_min);
                terrain_stats_.pixels_per_triangle_max = (std::max)(
                    terrain_stats_.pixels_per_triangle_max,
                    pixels_per_triangle_max);
            }

            const double total_weighted_sum =
                previous_weighted_sum + pixels_per_triangle_weighted_sum;
            terrain_stats_.pixels_per_triangle_weighted_mean =
                static_cast<float>(
                    total_weighted_sum
                    / static_cast<double>(
                        terrain_stats_.submitted_triangles));
        }
    }

    TerrainRenderStats RenderResourceResolver::terrain_render_stats()
        const noexcept
    {
        return terrain_stats_;
    }
}
