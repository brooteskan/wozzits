#include <scene/compile/terrain_visual_proxy_bridge.h>

#include <engine/assets/terrain/terrain_visual_proxy.h>

#include <utility>

namespace wz::scene
{
    namespace
    {
        AABB terrain_proxy_bounds_to_aabb(
            const wz::engine::assets::TerrainVisualProxyBounds& bounds)
        {
            return AABB{
                .min = { bounds.min[0], bounds.min[1], bounds.min[2] },
                .max = { bounds.max[0], bounds.max[1], bounds.max[2] },
            };
        }

        TerrainChunkId scene_terrain_chunk_id(
            wz::engine::assets::TerrainChunkId id) noexcept
        {
            return TerrainChunkId{ id.value };
        }

        TerrainLodId scene_terrain_lod_id(
            wz::engine::assets::TerrainLodId id) noexcept
        {
            return TerrainLodId{ id.value };
        }

        TerrainVisualRepresentationKind scene_terrain_representation_kind(
            wz::engine::assets::TerrainVisualRepresentationKind kind) noexcept
        {
            switch (kind) {
            case wz::engine::assets::TerrainVisualRepresentationKind::MeshChunks:
                return TerrainVisualRepresentationKind::MeshChunks;
            case wz::engine::assets::TerrainVisualRepresentationKind::GridTiles:
                return TerrainVisualRepresentationKind::GridTiles;
            case wz::engine::assets::TerrainVisualRepresentationKind::SurfelCloud:
                return TerrainVisualRepresentationKind::SurfelCloud;
            }
            return TerrainVisualRepresentationKind::MeshChunks;
        }

        TerrainVisualProxyBoundaryEdge scene_terrain_boundary_edge(
            wz::engine::assets::TerrainVisualProxyBoundaryEdge edge) noexcept
        {
            switch (edge) {
            case wz::engine::assets::TerrainVisualProxyBoundaryEdge::NegativeX:
                return TerrainVisualProxyBoundaryEdge::NegativeX;
            case wz::engine::assets::TerrainVisualProxyBoundaryEdge::PositiveX:
                return TerrainVisualProxyBoundaryEdge::PositiveX;
            case wz::engine::assets::TerrainVisualProxyBoundaryEdge::NegativeZ:
                return TerrainVisualProxyBoundaryEdge::NegativeZ;
            case wz::engine::assets::TerrainVisualProxyBoundaryEdge::PositiveZ:
                return TerrainVisualProxyBoundaryEdge::PositiveZ;
            }
            return TerrainVisualProxyBoundaryEdge::NegativeX;
        }

        TerrainVisualChunkBoundaryMetadata scene_terrain_boundary(
            const wz::engine::assets::TerrainVisualChunkBoundaryMetadata&
                boundary) noexcept
        {
            return TerrainVisualChunkBoundaryMetadata{
                .boundary_flags = boundary.boundary_flags,
                .negative_x_neighbor =
                    scene_terrain_chunk_id(boundary.negative_x_neighbor),
                .positive_x_neighbor =
                    scene_terrain_chunk_id(boundary.positive_x_neighbor),
                .negative_z_neighbor =
                    scene_terrain_chunk_id(boundary.negative_z_neighbor),
                .positive_z_neighbor =
                    scene_terrain_chunk_id(boundary.positive_z_neighbor),
            };
        }

        float chunk_asset_density(
            const wz::engine::assets::TerrainVisualProxyChunkRecord& chunk)
            noexcept
        {
            const float extent_x =
                chunk.bounds.max[0] - chunk.bounds.min[0];
            const float extent_z =
                chunk.bounds.max[2] - chunk.bounds.min[2];
            const float area = extent_x * extent_z;
            if (area <= 0.0f) {
                return 0.0f;
            }
            return static_cast<float>(chunk.triangle_count) / area;
        }
    }

    uint32_t terrain_visual_proxy_transition_strip_count(
        const wz::engine::assets::TerrainVisualProxyData* proxy) noexcept
    {
        return proxy ? proxy->transition_strip_count() : 0u;
    }

    void append_terrain_chunk_infos(
        std::vector<TerrainChunkInfo>& out,
        const TerrainVisualInstance& instance,
        uint32_t terrain_instance_index)
    {
        if (!instance.visual_proxy_data) {
            return;
        }

        const auto& proxy = *instance.visual_proxy_data;
        out.reserve(out.size() + proxy.chunks.size());
        for (const auto& chunk : proxy.chunks) {
            TerrainChunkInfo info{
                .terrain_instance_index = terrain_instance_index,
                .chunk_id = scene_terrain_chunk_id(chunk.chunk_id),
                .representation_kind =
                    scene_terrain_representation_kind(
                        chunk.representation_kind),
                .world_bounds = transform_aabb(
                    terrain_proxy_bounds_to_aabb(chunk.bounds),
                    instance.world),
                .asset_triangle_density = chunk_asset_density(chunk),
                .boundary = scene_terrain_boundary(chunk.boundary),
            };

            info.surfel_density_levels.reserve(
                chunk.surfel_density_levels.size());
            for (const auto& level : chunk.surfel_density_levels) {
                info.surfel_density_levels.push_back(
                    TerrainSurfelDensityLevel{
                        .density_id =
                            scene_terrain_lod_id(level.density_id),
                        .representative_radius =
                            level.representative_radius,
                        .equivalent_triangle_cost =
                            level.equivalent_triangle_cost,
                        .valid = level.valid(),
                    });
            }

            info.lods.reserve(chunk.lods.size());
            for (const auto& lod : chunk.lods) {
                info.lods.push_back(
                    TerrainLodRecord{
                        .lod_id = scene_terrain_lod_id(lod.lod_id),
                        .triangle_count = lod.triangle_count,
                        .conservative_geometric_error =
                            lod.conservative_geometric_error,
                        .valid = lod.valid(),
                    });
            }

            out.push_back(std::move(info));
        }
    }

    void append_terrain_transition_strips(
        std::vector<TerrainTransitionStripInfo>& out,
        const TerrainVisualInstance& instance)
    {
        if (!instance.visual_proxy_data) {
            return;
        }

        const auto& proxy = *instance.visual_proxy_data;
        out.reserve(out.size() + proxy.transition_strip_count());
        for (const auto& chunk : proxy.chunks) {
            for (const auto& strip : chunk.transition_strips) {
                out.push_back(
                    TerrainTransitionStripInfo{
                        .chunk_id =
                            scene_terrain_chunk_id(strip.chunk_id),
                        .neighbor_chunk_id =
                            scene_terrain_chunk_id(
                                strip.neighbor_chunk_id),
                        .lod_id = scene_terrain_lod_id(strip.lod_id),
                        .neighbor_lod_id =
                            scene_terrain_lod_id(strip.neighbor_lod_id),
                        .edge = scene_terrain_boundary_edge(strip.edge),
                    });
            }
        }
    }
}
