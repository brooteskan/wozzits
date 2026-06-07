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

    bool RenderResourceResolver::register_terrain_proxy(
        wz::engine::assets::TerrainProxyId terrain_proxy_id,
        wz::gpu::GPUHandle                       gpu_resource,
        wz::engine::assets::BuiltinRenderProgram program,
        wz::asset::ResourceHandle                render_program,
        wz::engine::assets::TerrainLightingData  terrain_lighting,
        float                                   terrain_target_pixels_per_triangle,
        wz::engine::assets::MeshRenderStyleData  mesh_style,
        std::span<const wz::engine::assets::TerrainVisualChunk> terrain_chunks,
        std::span<const TerrainFarSplatChunk> terrain_far_splat_chunks)
    {
        if (!terrain_proxy_id.valid()
            || !gpu_resource.valid()
            || terrain_chunks.empty())
        {
            return false;
        }

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

        for (auto& registered : terrain_proxy_entries_) {
            if (registered.first == terrain_proxy_id) {
                registered.second = std::move(entry);
                return true;
            }
        }

        terrain_proxy_entries_.push_back({
            terrain_proxy_id,
            std::move(entry),
        });
        return true;
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

    std::optional<ResolvedRenderableResource>
    RenderResourceResolver::resolve_terrain_proxy(
        wz::engine::assets::TerrainProxyId terrain_proxy_id) const noexcept
    {
        if (!terrain_proxy_id.valid())
            return std::nullopt;

        for (const auto& registered : terrain_proxy_entries_) {
            if (registered.first == terrain_proxy_id) {
                const Entry& e = registered.second;
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
        }

        return std::nullopt;
    }

    std::optional<ResolvedTerrainDrawResource>
    RenderResourceResolver::resolve_terrain_draw(
        wz::engine::assets::TerrainProxyId terrain_proxy_id,
        const wz::render::TerrainDrawRef& ref) const noexcept
    {
        if (!terrain_proxy_id.valid()
            || ref.representation_kind
                != wz::engine::assets::TerrainVisualRepresentationKind::MeshChunks)
        {
            return std::nullopt;
        }

        for (const auto& registered : terrain_proxy_entries_) {
            if (registered.first != terrain_proxy_id)
                continue;

            const Entry& e = registered.second;
            if (ref.chunk_id.value >= e.terrain_chunks.size())
                return std::nullopt;

            const auto& chunk = e.terrain_chunks[ref.chunk_id.value];
            if (chunk.index_count == 0)
                return std::nullopt;

            const bool replacement_available =
                chunk.replacement_index_count > 0
                && chunk.replacement_index_count < chunk.index_count;
            const bool replacement_selected =
                ref.lod_id.value > 0u && replacement_available;

            return ResolvedTerrainDrawResource{
                e.gpu_resource,
                e.program,
                e.render_program,
                e.terrain_lighting,
                e.terrain_target_pixels_per_triangle,
                replacement_selected
                    ? chunk.replacement_first_index
                    : chunk.first_index,
                replacement_selected
                    ? chunk.replacement_index_count
                    : chunk.index_count,
                chunk.triangle_count(),
                replacement_available,
                replacement_selected,
            };
        }

        return std::nullopt;
    }

    std::optional<wz::render::TerrainFrameDiagnostics>
    RenderResourceResolver::resolve_terrain_proxy_diagnostics(
        wz::engine::assets::TerrainProxyId terrain_proxy_id) const noexcept
    {
        if (!terrain_proxy_id.valid())
            return std::nullopt;

        for (const auto& registered : terrain_proxy_entries_) {
            if (registered.first != terrain_proxy_id)
                continue;

            const Entry& e = registered.second;
            wz::render::TerrainFrameDiagnostics diagnostics{};
            diagnostics.proxy_chunks =
                static_cast<uint64_t>(e.terrain_chunks.size());
            for (const auto& chunk : e.terrain_chunks)
                diagnostics.source_triangles += chunk.triangle_count();
            return diagnostics;
        }

        return std::nullopt;
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
        uint64_t pixels_per_triangle_triangles_le_256,
        uint32_t lod_level,
        wz::engine::assets::TerrainVisualRepresentationKind representation_kind,
        uint64_t submitted_draw_calls) const noexcept
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
        terrain_stats_.submitted_draw_calls += submitted_draw_calls > 0
            ? submitted_draw_calls
            : submitted_chunks;
        const uint32_t lod_bucket = (std::min)(
            lod_level,
            wz::render::kTerrainDiagnosticLodHistogramSize - 1u);
        terrain_stats_.lod_histogram[lod_bucket] += submitted_chunks;
        switch (representation_kind) {
        case wz::engine::assets::TerrainVisualRepresentationKind::MeshChunks:
            terrain_stats_.representation_counts.mesh_chunks += submitted_chunks;
            break;
        case wz::engine::assets::TerrainVisualRepresentationKind::GridTiles:
            terrain_stats_.representation_counts.grid_tiles += submitted_chunks;
            break;
        case wz::engine::assets::TerrainVisualRepresentationKind::SurfelCloud:
            terrain_stats_.representation_counts.surfel_clouds += submitted_chunks;
            break;
        }
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

    void RenderResourceResolver::record_terrain_source_totals(
        uint64_t proxy_chunks,
        uint64_t source_triangles) const noexcept
    {
        terrain_stats_.total_chunks += proxy_chunks;
        terrain_stats_.total_triangles += source_triangles;
    }

    void RenderResourceResolver::record_terrain_visible_chunks(
        uint64_t visible_chunks) const noexcept
    {
        terrain_stats_.visible_chunks += visible_chunks;
    }

    void RenderResourceResolver::record_terrain_projected_error_samples(
        std::span<const float> projected_error_px) const
    {
        const wz::render::TerrainFrameDiagnostics diagnostics =
            wz::render::terrain_projected_error_diagnostics(
                projected_error_px);
        terrain_stats_.projected_error_sample_count +=
            diagnostics.projected_error_sample_count;
        terrain_stats_.max_projected_error_px = (std::max)(
            terrain_stats_.max_projected_error_px,
            diagnostics.max_projected_error_px);
        terrain_stats_.median_projected_error_px = (std::max)(
            terrain_stats_.median_projected_error_px,
            diagnostics.median_projected_error_px);
        terrain_stats_.p95_projected_error_px = (std::max)(
            terrain_stats_.p95_projected_error_px,
            diagnostics.p95_projected_error_px);
    }

    void RenderResourceResolver::record_terrain_selector_cpu_us(
        double selector_cpu_us) const noexcept
    {
        terrain_stats_.selector_cpu_us += (std::max)(0.0, selector_cpu_us);
    }

    void RenderResourceResolver::record_terrain_gpu_us(
        double terrain_gpu_us) const noexcept
    {
        terrain_stats_.terrain_gpu_us += (std::max)(0.0, terrain_gpu_us);
        terrain_stats_.terrain_gpu_us_valid = true;
    }

    void RenderResourceResolver::record_terrain_budget_diagnostics(
        uint64_t budget_target_triangles,
        bool budget_missed) const noexcept
    {
        terrain_stats_.budget_target_triangles = (std::max)(
            terrain_stats_.budget_target_triangles,
            budget_target_triangles);
        if (budget_missed)
            ++terrain_stats_.budget_misses;
    }

    TerrainRenderStats RenderResourceResolver::terrain_render_stats()
        const noexcept
    {
        return terrain_stats_;
    }

    wz::render::TerrainFrameDiagnostics
    RenderResourceResolver::terrain_frame_diagnostics() const noexcept
    {
        wz::render::TerrainFrameDiagnostics diagnostics{};
        diagnostics.source_triangles = terrain_stats_.total_triangles;
        diagnostics.proxy_chunks = terrain_stats_.total_chunks;
        diagnostics.visible_chunks = terrain_stats_.visible_chunks;
        diagnostics.submitted_triangles = terrain_stats_.submitted_triangles;
        diagnostics.submitted_draw_calls =
            terrain_stats_.submitted_draw_calls;
        diagnostics.lod_histogram = terrain_stats_.lod_histogram;
        diagnostics.selector_cpu_us = terrain_stats_.selector_cpu_us;
        diagnostics.terrain_gpu_us = terrain_stats_.terrain_gpu_us;
        diagnostics.terrain_gpu_us_valid =
            terrain_stats_.terrain_gpu_us_valid;
        diagnostics.budget_target_triangles =
            terrain_stats_.budget_target_triangles;
        diagnostics.budget_misses = terrain_stats_.budget_misses;
        diagnostics.representation_counts =
            terrain_stats_.representation_counts;
        diagnostics.projected_error_sample_count =
            terrain_stats_.projected_error_sample_count;
        diagnostics.max_projected_error_px =
            terrain_stats_.max_projected_error_px;
        diagnostics.median_projected_error_px =
            terrain_stats_.median_projected_error_px;
        diagnostics.p95_projected_error_px =
            terrain_stats_.p95_projected_error_px;
        return diagnostics;
    }
}
