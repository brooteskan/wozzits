// src/engine/rendering/render_resource_resolver.cpp

#include <engine/rendering/render_resource_resolver.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
    // Shared lookup for the terrain-proxy entry list. Returns a pointer to the
    // registered Entry (mutable or const depending on the container), or
    // nullptr when the proxy id has no registration.
    template <typename Entries, typename ProxyId>
    auto find_terrain_proxy_entry(Entries& entries, ProxyId terrain_proxy_id)
        -> decltype(&entries.front().second)
    {
        const auto it = std::find_if(entries.begin(), entries.end(),
            [&](const auto& registered) {
                return registered.first == terrain_proxy_id;
            });
        return it != entries.end() ? &it->second : nullptr;
    }

    wz::engine::assets::TerrainProxyId engine_terrain_proxy_id(
        wz::scene::TerrainProxyId id) noexcept
    {
        return wz::engine::assets::TerrainProxyId{ id.key };
    }

    wz::engine::assets::TerrainChunkId engine_terrain_chunk_id(
        wz::scene::TerrainChunkId id) noexcept
    {
        return wz::engine::assets::TerrainChunkId{ id.value };
    }

    wz::engine::assets::TerrainLodId engine_terrain_lod_id(
        wz::scene::TerrainLodId id) noexcept
    {
        return wz::engine::assets::TerrainLodId{ id.value };
    }

    wz::engine::assets::TerrainVisualRepresentationKind
    engine_terrain_representation_kind(
        wz::scene::TerrainVisualRepresentationKind kind) noexcept
    {
        switch (kind) {
        case wz::scene::TerrainVisualRepresentationKind::MeshChunks:
            return wz::engine::assets::TerrainVisualRepresentationKind::MeshChunks;
        case wz::scene::TerrainVisualRepresentationKind::GridTiles:
            return wz::engine::assets::TerrainVisualRepresentationKind::GridTiles;
        case wz::scene::TerrainVisualRepresentationKind::SurfelCloud:
            return wz::engine::assets::TerrainVisualRepresentationKind::SurfelCloud;
        }
        return wz::engine::assets::TerrainVisualRepresentationKind::MeshChunks;
    }

    wz::engine::assets::TerrainVisualProxyBoundaryEdge
    engine_terrain_boundary_edge(
        wz::scene::TerrainVisualProxyBoundaryEdge edge) noexcept
    {
        switch (edge) {
        case wz::scene::TerrainVisualProxyBoundaryEdge::NegativeX:
            return wz::engine::assets::TerrainVisualProxyBoundaryEdge::NegativeX;
        case wz::scene::TerrainVisualProxyBoundaryEdge::PositiveX:
            return wz::engine::assets::TerrainVisualProxyBoundaryEdge::PositiveX;
        case wz::scene::TerrainVisualProxyBoundaryEdge::NegativeZ:
            return wz::engine::assets::TerrainVisualProxyBoundaryEdge::NegativeZ;
        case wz::scene::TerrainVisualProxyBoundaryEdge::PositiveZ:
            return wz::engine::assets::TerrainVisualProxyBoundaryEdge::PositiveZ;
        }
        return wz::engine::assets::TerrainVisualProxyBoundaryEdge::NegativeX;
    }

    bool float_equal(float a, float b) noexcept
    {
        return a == b || (std::isnan(a) && std::isnan(b));
    }

    bool color_equal(const float (&a)[4], const float (&b)[4]) noexcept
    {
        return float_equal(a[0], b[0])
            && float_equal(a[1], b[1])
            && float_equal(a[2], b[2])
            && float_equal(a[3], b[3]);
    }

    bool mask_rule_equal(
        const wz::engine::assets::MeshMaskRule& a,
        const wz::engine::assets::MeshMaskRule& b) noexcept
    {
        return a.enabled == b.enabled
            && a.input_channel_id == b.input_channel_id
            && float_equal(a.lo, b.lo)
            && float_equal(a.hi, b.hi)
            && color_equal(a.color, b.color)
            && a.priority == b.priority;
    }

    bool mask_style_equal(
        const wz::engine::assets::MeshMaskRenderStyleData& a,
        const wz::engine::assets::MeshMaskRenderStyleData& b) noexcept
    {
        if (a.enabled != b.enabled
            || a.domain != b.domain
            || a.projection_mode != b.projection_mode
            || a.overlap_mode != b.overlap_mode
            || !color_equal(a.unmatched_color, b.unmatched_color)
            || a.show_unmatched != b.show_unmatched
            || a.rules.size() != b.rules.size())
        {
            return false;
        }

        return std::equal(
            a.rules.begin(),
            a.rules.end(),
            b.rules.begin(),
            [](const wz::engine::assets::MeshMaskRule& lhs,
               const wz::engine::assets::MeshMaskRule& rhs) {
                return mask_rule_equal(lhs, rhs);
            });
    }

    void record_mesh_style_live_update(
        wz::engine::rendering::MeshStyleLiveUpdateStats& stats,
        const wz::engine::assets::MeshRenderStyleData& before,
        const wz::engine::assets::MeshRenderStyleData& after) noexcept
    {
        ++stats.style_updates;
        if (!mask_style_equal(before.mask, after.mask)) {
            ++stats.mask_rule_updates;
        }
    }
}

namespace wz::engine::rendering
{
    uint64_t RenderResourceResolver::terrain_far_splat_lookup_key(
        wz::engine::assets::TerrainChunkId chunk_id,
        wz::engine::assets::TerrainLodId density_id) noexcept
    {
        return (static_cast<uint64_t>(density_id.value) << 32u)
            | static_cast<uint64_t>(chunk_id.value);
    }

    void RenderResourceResolver::rebuild_terrain_far_splat_lookup(
        Entry& entry)
    {
        entry.terrain_far_splat_lookup.clear();
        entry.terrain_far_splat_lookup.reserve(
            entry.terrain_far_splat_chunks.size());
        for (size_t i = 0; i < entry.terrain_far_splat_chunks.size(); ++i) {
            const TerrainFarSplatChunk& chunk =
                entry.terrain_far_splat_chunks[i];
            entry.terrain_far_splat_lookup.push_back({
                terrain_far_splat_lookup_key(
                    chunk.chunk_id,
                    chunk.density_id),
                i,
            });
        }
        std::sort(
            entry.terrain_far_splat_lookup.begin(),
            entry.terrain_far_splat_lookup.end(),
            [](const Entry::TerrainFarSplatLookup& a,
               const Entry::TerrainFarSplatLookup& b)
            {
                return a.key < b.key;
            });
    }

    const TerrainFarSplatChunk*
    RenderResourceResolver::find_terrain_far_splat_chunk(
        const Entry& entry,
        wz::engine::assets::TerrainChunkId chunk_id,
        wz::engine::assets::TerrainLodId density_id)
    {
        const uint64_t key =
            terrain_far_splat_lookup_key(chunk_id, density_id);
        const auto found = std::lower_bound(
            entry.terrain_far_splat_lookup.begin(),
            entry.terrain_far_splat_lookup.end(),
            key,
            [](const Entry::TerrainFarSplatLookup& item, uint64_t value)
            {
                return item.key < value;
            });
        if (found == entry.terrain_far_splat_lookup.end()
            || found->key != key
            || found->index >= entry.terrain_far_splat_chunks.size())
        {
            return nullptr;
        }
        return &entry.terrain_far_splat_chunks[found->index];
    }

    wz::scene::SplatHandle RenderResourceResolver::register_splat_cloud(
        wz::gpu::GPUHandle                       gpu_resource,
        wz::engine::assets::BuiltinRenderProgram program,
        wz::asset::ResourceHandle                render_program)
    {
        const auto index =
            static_cast<wz::scene::SplatHandle>(splat_entries_.size());
        Entry entry{};
        entry.gpu_resource = gpu_resource;
        entry.program = program;
        entry.render_program = render_program;
        splat_entries_.push_back(std::move(entry));
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
            e.pulled_mesh,
            e.program,
            e.render_program,
            e.terrain_lighting,
            e.terrain_target_pixels_per_triangle,
            e.mesh_style,
            e.mesh_field_visualization_resource,
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
        std::span<const TerrainFarSplatChunk> terrain_far_splat_chunks,
        std::span<const TerrainTransitionDrawRange> terrain_transition_ranges,
        wz::gpu::GPUHandle                       mesh_field_visualization_resource)
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
        entry.mesh_field_visualization_resource =
            mesh_field_visualization_resource;
        entry.terrain_chunks.assign(
            terrain_chunks.begin(),
            terrain_chunks.end());
        entry.terrain_far_splat_chunks.assign(
            terrain_far_splat_chunks.begin(),
            terrain_far_splat_chunks.end());
        rebuild_terrain_far_splat_lookup(entry);
        entry.terrain_transition_ranges.assign(
            terrain_transition_ranges.begin(),
            terrain_transition_ranges.end());
        mesh_entries_.push_back(std::move(entry));
        return index;
    }

    wz::scene::MeshHandle RenderResourceResolver::register_pulled_mesh(
        ResolvedRenderableResource::PulledMeshResource pulled_mesh,
        wz::engine::assets::BuiltinRenderProgram program,
        wz::asset::ResourceHandle render_program,
        wz::engine::assets::MeshRenderStyleData mesh_style)
    {
        const auto index =
            static_cast<wz::scene::MeshHandle>(mesh_entries_.size());
        Entry entry{};
        entry.pulled_mesh = pulled_mesh;
        entry.program = program;
        entry.render_program = render_program;
        entry.mesh_style = std::move(mesh_style);
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
        std::span<const TerrainFarSplatChunk> terrain_far_splat_chunks,
        std::span<const TerrainTransitionDrawRange> terrain_transition_ranges,
        wz::gpu::GPUHandle                       mesh_field_visualization_resource)
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
        entry.mesh_field_visualization_resource =
            mesh_field_visualization_resource;
        entry.terrain_chunks.assign(
            terrain_chunks.begin(),
            terrain_chunks.end());
        entry.terrain_far_splat_chunks.assign(
            terrain_far_splat_chunks.begin(),
            terrain_far_splat_chunks.end());
        rebuild_terrain_far_splat_lookup(entry);
        entry.terrain_transition_ranges.assign(
            terrain_transition_ranges.begin(),
            terrain_transition_ranges.end());

        if (auto* registered = find_terrain_proxy_entry(
                terrain_proxy_entries_, terrain_proxy_id))
        {
            *registered = std::move(entry);
            return true;
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
            e.pulled_mesh,
            e.program,
            e.render_program,
            e.terrain_lighting,
            e.terrain_target_pixels_per_triangle,
            e.mesh_style,
            e.mesh_field_visualization_resource,
            e.terrain_chunks,
            e.terrain_far_splat_chunks };
    }

    bool RenderResourceResolver::update_mesh_style(
        wz::scene::MeshHandle handle,
        wz::engine::assets::MeshRenderStyleData style) noexcept
    {
        if (!style.valid())
            return false;
        if (handle == wz::scene::INVALID_MESH)
            return false;
        if (static_cast<size_t>(handle) >= mesh_entries_.size())
            return false;

        Entry& e = mesh_entries_[handle];
        record_mesh_style_live_update(
            mesh_style_live_update_stats_,
            e.mesh_style,
            style);
        e.mesh_style = std::move(style);
        return true;
    }

    std::optional<ResolvedRenderableResource>
    RenderResourceResolver::resolve_terrain_proxy(
        wz::engine::assets::TerrainProxyId terrain_proxy_id) const noexcept
    {
        if (!terrain_proxy_id.valid())
            return std::nullopt;

        const Entry* e = find_terrain_proxy_entry(
            terrain_proxy_entries_, terrain_proxy_id);
        if (!e)
            return std::nullopt;

        return ResolvedRenderableResource{
            e->gpu_resource,
            e->pulled_mesh,
            e->program,
            e->render_program,
            e->terrain_lighting,
            e->terrain_target_pixels_per_triangle,
            e->mesh_style,
            e->mesh_field_visualization_resource,
            e->terrain_chunks,
            e->terrain_far_splat_chunks };
    }

    bool RenderResourceResolver::update_terrain_proxy_mesh_style(
        wz::engine::assets::TerrainProxyId terrain_proxy_id,
        wz::engine::assets::MeshRenderStyleData style) noexcept
    {
        if (!style.valid())
            return false;
        if (!terrain_proxy_id.valid())
            return false;

        Entry* e = find_terrain_proxy_entry(
            terrain_proxy_entries_,
            terrain_proxy_id);
        if (!e)
            return false;

        record_mesh_style_live_update(
            mesh_style_live_update_stats_,
            e->mesh_style,
            style);
        e->mesh_style = std::move(style);
        return true;
    }

    bool RenderResourceResolver::update_terrain_proxy_mesh_style(
        wz::scene::TerrainProxyId terrain_proxy_id,
        wz::engine::assets::MeshRenderStyleData style) noexcept
    {
        return update_terrain_proxy_mesh_style(
            engine_terrain_proxy_id(terrain_proxy_id),
            std::move(style));
    }

    std::optional<ResolvedTerrainDrawResource>
    RenderResourceResolver::resolve_terrain_draw(
        wz::scene::TerrainProxyId terrain_proxy_id,
        const wz::render::TerrainDrawRef& ref) const noexcept
    {
        return resolve_terrain_draw(
            engine_terrain_proxy_id(terrain_proxy_id),
            ref);
    }

    std::optional<ResolvedTerrainDrawResource>
    RenderResourceResolver::resolve_terrain_draw(
        wz::engine::assets::TerrainProxyId terrain_proxy_id,
        const wz::render::TerrainDrawRef& ref) const noexcept
    {
        if (!terrain_proxy_id.valid()) {
            return std::nullopt;
        }

        const Entry* found = find_terrain_proxy_entry(
            terrain_proxy_entries_, terrain_proxy_id);
        if (!found)
            return std::nullopt;

        const Entry& e = *found;
        const auto representation_kind =
            engine_terrain_representation_kind(ref.representation_kind);
        const auto chunk_id = engine_terrain_chunk_id(ref.chunk_id);
        const auto neighbor_chunk_id =
            engine_terrain_chunk_id(ref.neighbor_chunk_id);
        const auto lod_id = engine_terrain_lod_id(ref.lod_id);
        const auto neighbor_lod_id =
            engine_terrain_lod_id(ref.neighbor_lod_id);
        const auto transition_edge =
            engine_terrain_boundary_edge(ref.transition_edge);
        if (ref.kind == wz::render::TerrainDrawRefKind::LodTransition) {
            if (representation_kind
                != wz::engine::assets::TerrainVisualRepresentationKind
                    ::MeshChunks)
            {
                return std::nullopt;
            }
            const auto range_it = std::find_if(
                e.terrain_transition_ranges.begin(),
                e.terrain_transition_ranges.end(),
                [&](const TerrainTransitionDrawRange& range) {
                    return range.chunk_id == chunk_id
                        && range.neighbor_chunk_id == neighbor_chunk_id
                        && range.lod_id == lod_id
                        && range.neighbor_lod_id == neighbor_lod_id
                        && range.edge == transition_edge
                        && range.index_count > 0u;
                });
            if (range_it == e.terrain_transition_ranges.end())
                return std::nullopt;

            const TerrainTransitionDrawRange& range = *range_it;
            return ResolvedTerrainDrawResource{
                .gpu_resource = e.gpu_resource,
                .program = e.program,
                .render_program = e.render_program,
                .terrain_lighting = e.terrain_lighting,
                .terrain_target_pixels_per_triangle =
                    e.terrain_target_pixels_per_triangle,
                .first_index = range.first_index,
                .index_count = range.index_count,
                .source_triangle_count = range.triangle_count(),
                .transition_selected = true,
            };
        }

        if (representation_kind
            == wz::engine::assets::TerrainVisualRepresentationKind
                ::SurfelCloud)
        {
            const TerrainFarSplatChunk* chunk =
                find_terrain_far_splat_chunk(
                    e,
                    chunk_id,
                    lod_id);
            if (chunk && chunk->splat_count > 0u) {
                return ResolvedTerrainDrawResource{
                    .gpu_resource = chunk->gpu_resource,
                    .program = e.program,
                    .render_program = e.render_program,
                    .terrain_lighting = e.terrain_lighting,
                    .terrain_target_pixels_per_triangle =
                        e.terrain_target_pixels_per_triangle,
                    .source_triangle_count =
                        chunk_id.value < e.terrain_chunks.size()
                        ? e.terrain_chunks[chunk_id.value]
                            .triangle_count()
                        : 0u,
                    .first_splat = chunk->first_splat,
                    .far_splat_count = chunk->splat_count,
                    .far_splat_selected = true,
                };
            }
            return std::nullopt;
        }

        if (representation_kind
            != wz::engine::assets::TerrainVisualRepresentationKind
                ::MeshChunks)
        {
            return std::nullopt;
        }

        if (chunk_id.value >= e.terrain_chunks.size())
            return std::nullopt;

        const auto& chunk = e.terrain_chunks[chunk_id.value];
        if (chunk.index_count == 0)
            return std::nullopt;

        const bool replacement_available =
            chunk.replacement_index_count > 0
            && chunk.replacement_index_count < chunk.index_count;
        const bool replacement_selected =
            ref.lod_id.value > 0u && replacement_available;

        return ResolvedTerrainDrawResource{
            .gpu_resource = e.gpu_resource,
            .program = e.program,
            .render_program = e.render_program,
            .terrain_lighting = e.terrain_lighting,
            .terrain_target_pixels_per_triangle =
                e.terrain_target_pixels_per_triangle,
            .first_index = replacement_selected
                ? chunk.replacement_first_index
                : chunk.first_index,
            .index_count = replacement_selected
                ? chunk.replacement_index_count
                : chunk.index_count,
            .source_triangle_count = chunk.triangle_count(),
            .lod_replacement_available = replacement_available,
            .lod_replacement_selected = replacement_selected,
        };
    }

    std::optional<ResolvedTerrainDrawResource>
    RenderResourceResolver::resolve_terrain_draw_mesh_fallback(
        wz::scene::TerrainProxyId terrain_proxy_id,
        const wz::render::TerrainDrawRef& ref) const noexcept
    {
        return resolve_terrain_draw_mesh_fallback(
            engine_terrain_proxy_id(terrain_proxy_id),
            ref);
    }

    std::optional<ResolvedTerrainDrawResource>
    RenderResourceResolver::resolve_terrain_draw_mesh_fallback(
        wz::engine::assets::TerrainProxyId terrain_proxy_id,
        const wz::render::TerrainDrawRef& ref) const noexcept
    {
        if (!terrain_proxy_id.valid()) {
            return std::nullopt;
        }

        if (ref.kind != wz::render::TerrainDrawRefKind::ChunkLod) {
            return std::nullopt;
        }

        const Entry* found = find_terrain_proxy_entry(
            terrain_proxy_entries_, terrain_proxy_id);
        if (!found)
            return std::nullopt;

        const Entry& e = *found;
        const auto chunk_id = engine_terrain_chunk_id(ref.chunk_id);
        if (chunk_id.value >= e.terrain_chunks.size())
            return std::nullopt;

        const auto& chunk = e.terrain_chunks[chunk_id.value];
        if (chunk.index_count == 0u)
            return std::nullopt;

        const bool replacement_available =
            chunk.replacement_index_count > 0u
            && chunk.replacement_index_count < chunk.index_count;

        return ResolvedTerrainDrawResource{
            .gpu_resource = e.gpu_resource,
            .program = e.program,
            .render_program = e.render_program,
            .terrain_lighting = e.terrain_lighting,
            .terrain_target_pixels_per_triangle =
                e.terrain_target_pixels_per_triangle,
            .first_index = replacement_available
                ? chunk.replacement_first_index
                : chunk.first_index,
            .index_count = replacement_available
                ? chunk.replacement_index_count
                : chunk.index_count,
            .source_triangle_count = chunk.triangle_count(),
            .lod_replacement_available = replacement_available,
            .lod_replacement_selected = replacement_available,
        };
    }

    std::optional<wz::render::TerrainFrameDiagnostics>
    RenderResourceResolver::resolve_terrain_proxy_diagnostics(
        wz::scene::TerrainProxyId terrain_proxy_id) const noexcept
    {
        return resolve_terrain_proxy_diagnostics(
            engine_terrain_proxy_id(terrain_proxy_id));
    }

    std::optional<wz::render::TerrainFrameDiagnostics>
    RenderResourceResolver::resolve_terrain_proxy_diagnostics(
        wz::engine::assets::TerrainProxyId terrain_proxy_id) const noexcept
    {
        if (!terrain_proxy_id.valid())
            return std::nullopt;

        const Entry* e = find_terrain_proxy_entry(
            terrain_proxy_entries_, terrain_proxy_id);
        if (!e)
            return std::nullopt;

        wz::render::TerrainFrameDiagnostics diagnostics{};
        diagnostics.proxy_chunks =
            static_cast<uint64_t>(e->terrain_chunks.size());
        for (const auto& chunk : e->terrain_chunks)
            diagnostics.source_triangles += chunk.triangle_count();
        return diagnostics;
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
        terrain_stats_.submitted_draw_calls +=
            submitted_draw_calls
                == kTerrainSubmittedDrawCallsUseSubmittedChunks
            ? submitted_chunks
            : submitted_draw_calls;
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

    void RenderResourceResolver::record_terrain_submit_cpu_profile(
        double total_us,
        double resolve_us,
        double resource_us,
        double constants_us,
        double bind_us,
        double draw_us,
        double stats_us,
        uint64_t surfel_draw_calls,
        uint64_t mesh_draw_calls,
        uint64_t fallback_mesh_draw_calls,
        uint64_t surfel_fallbacks) const noexcept
    {
        terrain_stats_.terrain_submit_cpu_us += (std::max)(0.0, total_us);
        terrain_stats_.terrain_submit_resolve_cpu_us +=
            (std::max)(0.0, resolve_us);
        terrain_stats_.terrain_submit_resource_cpu_us +=
            (std::max)(0.0, resource_us);
        terrain_stats_.terrain_submit_constants_cpu_us +=
            (std::max)(0.0, constants_us);
        terrain_stats_.terrain_submit_bind_cpu_us +=
            (std::max)(0.0, bind_us);
        terrain_stats_.terrain_submit_draw_cpu_us +=
            (std::max)(0.0, draw_us);
        terrain_stats_.terrain_submit_stats_cpu_us +=
            (std::max)(0.0, stats_us);
        terrain_stats_.terrain_submit_surfel_draw_calls +=
            surfel_draw_calls;
        terrain_stats_.terrain_submit_mesh_draw_calls += mesh_draw_calls;
        terrain_stats_.terrain_submit_fallback_mesh_draw_calls +=
            fallback_mesh_draw_calls;
        terrain_stats_.terrain_submit_surfel_fallbacks += surfel_fallbacks;
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

    void RenderResourceResolver::reset_mesh_style_live_update_stats()
        const noexcept
    {
        mesh_style_live_update_stats_ = {};
    }

    MeshStyleLiveUpdateStats
    RenderResourceResolver::mesh_style_live_update_stats() const noexcept
    {
        return mesh_style_live_update_stats_;
    }
}
