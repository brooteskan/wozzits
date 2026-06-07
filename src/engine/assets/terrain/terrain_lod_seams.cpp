#include <engine/assets/terrain/terrain_lod_seams.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace wz::engine::assets
{
    namespace
    {
        float distance3(
            const TerrainVisualProxyBoundaryPoint& a,
            const TerrainVisualProxyBoundaryPoint& b) noexcept
        {
            const float dx = a.position[0] - b.position[0];
            const float dy = a.position[1] - b.position[1];
            const float dz = a.position[2] - b.position[2];
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        const TerrainVisualProxyChunkRecord* find_chunk(
            const TerrainVisualProxyData& proxy,
            TerrainChunkId chunk_id) noexcept
        {
            for (const TerrainVisualProxyChunkRecord& chunk : proxy.chunks) {
                if (chunk.chunk_id == chunk_id) {
                    return &chunk;
                }
            }
            return nullptr;
        }

        const TerrainVisualProxyLodRecord* find_lod(
            const TerrainVisualProxyChunkRecord& chunk,
            TerrainLodId lod_id) noexcept
        {
            for (const TerrainVisualProxyLodRecord& lod : chunk.lods) {
                if (lod.lod_id == lod_id) {
                    return &lod;
                }
            }
            return nullptr;
        }

        TerrainLodId selected_lod_or_default(
            const std::unordered_map<uint32_t, TerrainLodId>& selections,
            TerrainChunkId chunk_id) noexcept
        {
            const auto it = selections.find(chunk_id.value);
            if (it != selections.end()) {
                return it->second;
            }
            return TerrainLodId{ 0u };
        }

        float directed_max_gap(
            const std::vector<TerrainVisualProxyBoundaryPoint>& from,
            const std::vector<TerrainVisualProxyBoundaryPoint>& to) noexcept
        {
            if (from.empty() || to.empty()) {
                return (std::numeric_limits<float>::infinity)();
            }

            float max_gap = 0.0f;
            for (const TerrainVisualProxyBoundaryPoint& point : from) {
                float nearest = (std::numeric_limits<float>::infinity)();
                for (const TerrainVisualProxyBoundaryPoint& candidate : to) {
                    nearest = std::min(nearest, distance3(point, candidate));
                }
                max_gap = std::max(max_gap, nearest);
            }
            return max_gap;
        }

        TerrainLodSeamReport analyze_terrain_lod_seam_records(
            TerrainLodSeamEndpoint a,
            const TerrainVisualProxyLodRecord& lod_a,
            TerrainLodSeamEndpoint b,
            const TerrainVisualProxyLodRecord& lod_b,
            float tolerance)
        {
            TerrainLodSeamReport report{};
            report.a = a;
            report.b = b;
            report.needs_transition = a.lod_id != b.lod_id;

            const auto* points_a = terrain_lod_boundary_points(lod_a, a.edge);
            const auto* points_b = terrain_lod_boundary_points(lod_b, b.edge);
            if (!points_a || !points_b || points_a->empty()
                || points_b->empty())
            {
                report.gap_exceeds_tolerance = true;
                return report;
            }

            report.a_boundary_points = static_cast<uint32_t>(points_a->size());
            report.b_boundary_points = static_cast<uint32_t>(points_b->size());
            report.max_boundary_gap = std::max(
                directed_max_gap(*points_a, *points_b),
                directed_max_gap(*points_b, *points_a));
            report.gap_exceeds_tolerance = report.max_boundary_gap > tolerance;
            return report;
        }

        void maybe_push_neighbor_report(
            const std::unordered_map<
                uint32_t,
                const TerrainVisualProxyChunkRecord*>& chunks,
            const std::unordered_map<uint32_t, TerrainLodId>& selections,
            float tolerance,
            std::vector<TerrainLodSeamReport>& out,
            const TerrainVisualProxyChunkRecord& chunk,
            TerrainChunkId neighbor,
            TerrainVisualProxyBoundaryEdge edge)
        {
            if (neighbor == kInvalidTerrainChunkId
                || chunk.chunk_id.value >= neighbor.value)
            {
                return;
            }

            const TerrainLodId chunk_lod =
                selected_lod_or_default(selections, chunk.chunk_id);
            const TerrainLodId neighbor_lod =
                selected_lod_or_default(selections, neighbor);

            const TerrainLodSeamEndpoint a{
                .chunk_id = chunk.chunk_id,
                .lod_id = chunk_lod,
                .edge = edge,
            };
            const TerrainLodSeamEndpoint b{
                .chunk_id = neighbor,
                .lod_id = neighbor_lod,
                .edge = opposite_terrain_boundary_edge(edge),
            };

            TerrainLodSeamReport report{};
            report.a = a;
            report.b = b;
            report.needs_transition = a.lod_id != b.lod_id;

            const auto neighbor_it = chunks.find(neighbor.value);
            if (neighbor_it == chunks.end()) {
                report.gap_exceeds_tolerance = true;
                out.push_back(report);
                return;
            }

            const TerrainVisualProxyLodRecord* lod_a = find_lod(chunk, chunk_lod);
            const TerrainVisualProxyLodRecord* lod_b =
                find_lod(*neighbor_it->second, neighbor_lod);
            if (!lod_a || !lod_b) {
                report.gap_exceeds_tolerance = true;
                out.push_back(report);
                return;
            }

            out.push_back(analyze_terrain_lod_seam_records(
                a,
                *lod_a,
                b,
                *lod_b,
                tolerance));
        }
    }

    const std::vector<TerrainVisualProxyBoundaryPoint>*
    terrain_lod_boundary_points(
        const TerrainVisualProxyLodRecord& lod,
        TerrainVisualProxyBoundaryEdge edge) noexcept
    {
        switch (edge) {
        case TerrainVisualProxyBoundaryEdge::NegativeX:
            return &lod.boundary_ring.negative_x;
        case TerrainVisualProxyBoundaryEdge::PositiveX:
            return &lod.boundary_ring.positive_x;
        case TerrainVisualProxyBoundaryEdge::NegativeZ:
            return &lod.boundary_ring.negative_z;
        case TerrainVisualProxyBoundaryEdge::PositiveZ:
            return &lod.boundary_ring.positive_z;
        }
        return nullptr;
    }

    TerrainVisualProxyBoundaryEdge opposite_terrain_boundary_edge(
        TerrainVisualProxyBoundaryEdge edge) noexcept
    {
        switch (edge) {
        case TerrainVisualProxyBoundaryEdge::NegativeX:
            return TerrainVisualProxyBoundaryEdge::PositiveX;
        case TerrainVisualProxyBoundaryEdge::PositiveX:
            return TerrainVisualProxyBoundaryEdge::NegativeX;
        case TerrainVisualProxyBoundaryEdge::NegativeZ:
            return TerrainVisualProxyBoundaryEdge::PositiveZ;
        case TerrainVisualProxyBoundaryEdge::PositiveZ:
            return TerrainVisualProxyBoundaryEdge::NegativeZ;
        }
        return TerrainVisualProxyBoundaryEdge::NegativeX;
    }

    TerrainLodSeamReport analyze_terrain_lod_seam(
        const TerrainVisualProxyData& proxy,
        TerrainLodSeamEndpoint a,
        TerrainLodSeamEndpoint b,
        float tolerance)
    {
        const TerrainVisualProxyChunkRecord* chunk_a =
            find_chunk(proxy, a.chunk_id);
        const TerrainVisualProxyChunkRecord* chunk_b =
            find_chunk(proxy, b.chunk_id);
        TerrainLodSeamReport report{};
        report.a = a;
        report.b = b;
        report.needs_transition = a.lod_id != b.lod_id;
        if (!chunk_a || !chunk_b) {
            report.gap_exceeds_tolerance = true;
            return report;
        }

        const TerrainVisualProxyLodRecord* lod_a = find_lod(*chunk_a, a.lod_id);
        const TerrainVisualProxyLodRecord* lod_b = find_lod(*chunk_b, b.lod_id);
        if (!lod_a || !lod_b) {
            report.gap_exceeds_tolerance = true;
            return report;
        }

        return analyze_terrain_lod_seam_records(
            a,
            *lod_a,
            b,
            *lod_b,
            tolerance);
    }

    std::vector<TerrainLodSeamReport> analyze_adjacent_terrain_lod_seams(
        const TerrainVisualProxyData& proxy,
        std::span<const TerrainLodSelection> selections,
        float tolerance)
    {
        std::unordered_map<uint32_t, const TerrainVisualProxyChunkRecord*>
            chunks_by_id;
        chunks_by_id.reserve(proxy.chunks.size());
        for (const TerrainVisualProxyChunkRecord& chunk : proxy.chunks) {
            chunks_by_id.emplace(chunk.chunk_id.value, &chunk);
        }

        std::unordered_map<uint32_t, TerrainLodId> selected_lods;
        selected_lods.reserve(selections.size());
        for (const TerrainLodSelection& selection : selections) {
            selected_lods[selection.chunk_id.value] = selection.lod_id;
        }

        std::vector<TerrainLodSeamReport> out;
        for (const TerrainVisualProxyChunkRecord& chunk : proxy.chunks) {
            maybe_push_neighbor_report(
                chunks_by_id,
                selected_lods,
                tolerance,
                out,
                chunk,
                chunk.boundary.positive_x_neighbor,
                TerrainVisualProxyBoundaryEdge::PositiveX);
            maybe_push_neighbor_report(
                chunks_by_id,
                selected_lods,
                tolerance,
                out,
                chunk,
                chunk.boundary.positive_z_neighbor,
                TerrainVisualProxyBoundaryEdge::PositiveZ);
            maybe_push_neighbor_report(
                chunks_by_id,
                selected_lods,
                tolerance,
                out,
                chunk,
                chunk.boundary.negative_x_neighbor,
                TerrainVisualProxyBoundaryEdge::NegativeX);
            maybe_push_neighbor_report(
                chunks_by_id,
                selected_lods,
                tolerance,
                out,
                chunk,
                chunk.boundary.negative_z_neighbor,
                TerrainVisualProxyBoundaryEdge::NegativeZ);
        }
        return out;
    }
}
