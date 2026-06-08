#pragma once

#include <scene/compile/compiled_scene.h>

#include <span>
#include <vector>

namespace wz::scene
{
    struct TerrainChunkInfo
    {
        uint32_t terrain_instance_index = 0;
        wz::engine::assets::TerrainChunkId chunk_id{};
        wz::engine::assets::TerrainVisualRepresentationKind representation_kind =
            wz::engine::assets::TerrainVisualRepresentationKind::MeshChunks;
        AABB world_bounds{};
        wz::engine::assets::TerrainVisualChunkBoundaryMetadata boundary{};
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
