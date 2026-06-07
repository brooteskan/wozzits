#pragma once

#include <engine/assets/terrain/terrain_visual_proxy.h>

#include <cstdint>
#include <span>
#include <vector>

namespace wz::engine::assets
{
    enum class TerrainVisualProxyBoundaryEdge : uint8_t
    {
        NegativeX,
        PositiveX,
        NegativeZ,
        PositiveZ,
    };

    struct TerrainLodSelection
    {
        TerrainChunkId chunk_id{};
        TerrainLodId lod_id{};
    };

    struct TerrainLodSeamEndpoint
    {
        TerrainChunkId chunk_id{};
        TerrainLodId lod_id{};
        TerrainVisualProxyBoundaryEdge edge =
            TerrainVisualProxyBoundaryEdge::NegativeX;
    };

    struct TerrainLodSeamReport
    {
        TerrainLodSeamEndpoint a{};
        TerrainLodSeamEndpoint b{};
        uint32_t a_boundary_points = 0;
        uint32_t b_boundary_points = 0;
        float max_boundary_gap = 0.0f;
        bool needs_transition = false;
        bool gap_exceeds_tolerance = false;

        [[nodiscard]] bool valid() const noexcept
        {
            return a_boundary_points > 0u && b_boundary_points > 0u;
        }
    };

    [[nodiscard]] const std::vector<TerrainVisualProxyBoundaryPoint>*
    terrain_lod_boundary_points(
        const TerrainVisualProxyLodRecord& lod,
        TerrainVisualProxyBoundaryEdge edge) noexcept;

    [[nodiscard]] TerrainVisualProxyBoundaryEdge opposite_terrain_boundary_edge(
        TerrainVisualProxyBoundaryEdge edge) noexcept;

    [[nodiscard]] TerrainLodSeamReport analyze_terrain_lod_seam(
        const TerrainVisualProxyData& proxy,
        TerrainLodSeamEndpoint a,
        TerrainLodSeamEndpoint b,
        float tolerance);

    [[nodiscard]] std::vector<TerrainLodSeamReport>
    analyze_adjacent_terrain_lod_seams(
        const TerrainVisualProxyData& proxy,
        std::span<const TerrainLodSelection> selections,
        float tolerance);
}
