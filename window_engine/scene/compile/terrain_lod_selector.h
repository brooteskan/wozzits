#pragma once

#include <scene/compile/compiled_scene.h>

#include <span>
#include <vector>

namespace wz::scene
{
    struct TerrainSurfelDensityLevel
    {
        TerrainLodId density_id{};
        float representative_radius = 0.0f;
        uint32_t equivalent_triangle_cost = 0u;
        bool valid = false;
    };

    struct TerrainLodRecord
    {
        TerrainLodId lod_id{};
        uint32_t triangle_count = 0u;
        float conservative_geometric_error = 0.0f;
        bool valid = false;
    };

    struct TerrainChunkInfo
    {
        uint32_t terrain_instance_index = 0;
        TerrainChunkId chunk_id{};
        TerrainVisualRepresentationKind representation_kind =
            TerrainVisualRepresentationKind::MeshChunks;
        AABB world_bounds{};
        float asset_triangle_density = 0.0f;
        TerrainVisualChunkBoundaryMetadata boundary{};
        std::vector<TerrainSurfelDensityLevel> surfel_density_levels;
        std::vector<TerrainLodRecord> lods;
    };

    std::vector<TerrainLodChoice> select_terrain_lods(
        std::span<const TerrainChunkInfo> chunks,
        const TerrainLodSelectionParams& params,
        const ViewData& view,
        std::span<const TerrainLodChoice> previous = {});
}
