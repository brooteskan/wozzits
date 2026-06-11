#pragma once

#include <scene/compile/compiled_scene.h>

#include <span>
#include <vector>

namespace wz::scene
{
    struct TerrainChunkInfo
    {
        uint32_t terrain_instance_index = 0;
        TerrainChunkId chunk_id{};
        TerrainVisualRepresentationKind representation_kind =
            TerrainVisualRepresentationKind::MeshChunks;
        AABB world_bounds{};
        float asset_triangle_density = 0.0f;
        TerrainVisualChunkBoundaryMetadata boundary{};
        std::span<
            const wz::engine::assets::TerrainVisualProxySurfelDensityLevel>
            surfel_density_levels;
        std::span<const wz::engine::assets::TerrainVisualProxyLodRecord> lods;
    };

    std::vector<TerrainLodChoice> select_terrain_lods(
        std::span<const TerrainChunkInfo> chunks,
        const TerrainLodSelectionParams& params,
        const ViewData& view,
        std::span<const TerrainLodChoice> previous = {});
}
