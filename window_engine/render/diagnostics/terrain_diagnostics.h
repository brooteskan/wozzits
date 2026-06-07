#pragma once

#include <array>
#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace wz::render
{
    inline constexpr uint32_t kTerrainDiagnosticLodHistogramSize = 16;

    struct TerrainRepresentationCounts
    {
        uint64_t mesh_chunks = 0;
        uint64_t grid_tiles = 0;
        uint64_t surfel_clouds = 0;
    };

    struct TerrainFrameDiagnostics
    {
        uint64_t source_triangles = 0;
        uint64_t proxy_chunks = 0;
        uint64_t visible_chunks = 0;
        uint64_t submitted_triangles = 0;
        uint64_t submitted_draw_calls = 0;
        std::array<uint64_t, kTerrainDiagnosticLodHistogramSize> lod_histogram{};
        double selector_cpu_us = 0.0;
        double terrain_gpu_us = 0.0;
        bool terrain_gpu_us_valid = false;
        uint64_t budget_target_triangles = 0;
        uint64_t budget_misses = 0;
        TerrainRepresentationCounts representation_counts{};
        uint64_t projected_error_sample_count = 0;
        float max_projected_error_px = 0.0f;
        float median_projected_error_px = 0.0f;
        float p95_projected_error_px = 0.0f;
    };

    struct TerrainDiagnosticsSummary
    {
        TerrainFrameDiagnostics totals{};
        uint64_t frame_count = 0;
    };

    inline void add_terrain_frame_diagnostics(
        TerrainFrameDiagnostics& dst,
        const TerrainFrameDiagnostics& src) noexcept
    {
        dst.source_triangles += src.source_triangles;
        dst.proxy_chunks += src.proxy_chunks;
        dst.visible_chunks += src.visible_chunks;
        dst.submitted_triangles += src.submitted_triangles;
        dst.submitted_draw_calls += src.submitted_draw_calls;
        for (uint32_t i = 0; i < kTerrainDiagnosticLodHistogramSize; ++i)
            dst.lod_histogram[i] += src.lod_histogram[i];
        dst.selector_cpu_us += src.selector_cpu_us;
        if (src.terrain_gpu_us_valid) {
            dst.terrain_gpu_us += src.terrain_gpu_us;
            dst.terrain_gpu_us_valid = true;
        }
        dst.budget_target_triangles += src.budget_target_triangles;
        dst.budget_misses += src.budget_misses;
        dst.representation_counts.mesh_chunks +=
            src.representation_counts.mesh_chunks;
        dst.representation_counts.grid_tiles +=
            src.representation_counts.grid_tiles;
        dst.representation_counts.surfel_clouds +=
            src.representation_counts.surfel_clouds;
        dst.projected_error_sample_count +=
            src.projected_error_sample_count;
        dst.max_projected_error_px = (std::max)(
            dst.max_projected_error_px,
            src.max_projected_error_px);
        dst.median_projected_error_px = (std::max)(
            dst.median_projected_error_px,
            src.median_projected_error_px);
        dst.p95_projected_error_px = (std::max)(
            dst.p95_projected_error_px,
            src.p95_projected_error_px);
    }

    inline TerrainFrameDiagnostics terrain_projected_error_diagnostics(
        std::span<const float> projected_error_px)
    {
        TerrainFrameDiagnostics out{};
        out.projected_error_sample_count = projected_error_px.size();
        if (projected_error_px.empty())
            return out;

        std::vector<float> sorted;
        sorted.reserve(projected_error_px.size());
        for (float value : projected_error_px) {
            if (value >= 0.0f)
                sorted.push_back(value);
        }
        out.projected_error_sample_count = sorted.size();
        if (sorted.empty())
            return out;

        std::sort(sorted.begin(), sorted.end());
        const auto percentile = [&sorted](float p) {
            const size_t index = static_cast<size_t>(
                (static_cast<float>(sorted.size() - 1u) * p) + 0.5f);
            return sorted[(std::min)(index, sorted.size() - 1u)];
        };

        out.max_projected_error_px = sorted.back();
        out.median_projected_error_px = percentile(0.5f);
        out.p95_projected_error_px = percentile(0.95f);
        return out;
    }

    class TerrainDiagnosticsAccumulator
    {
    public:
        void reset() noexcept
        {
            summary_ = {};
        }

        void add_frame(const TerrainFrameDiagnostics& frame) noexcept
        {
            add_terrain_frame_diagnostics(summary_.totals, frame);
            ++summary_.frame_count;
        }

        [[nodiscard]] const TerrainDiagnosticsSummary& summary()
            const noexcept
        {
            return summary_;
        }

    private:
        TerrainDiagnosticsSummary summary_{};
    };
}
