#include <engine/assets/terrain/terrain_visual_proxy.h>

#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace wz::engine::assets
{
    namespace
    {
        float clamp01(float value) noexcept
        {
            return std::clamp(value, 0.0f, 1.0f);
        }

        float lerp(float a, float b, float weight) noexcept
        {
            return a + (b - a) * weight;
        }

        float vector_length(const float v[3]) noexcept
        {
            return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        }

        void normalize_or_up(float v[3]) noexcept
        {
            const float length = vector_length(v);
            if (length <= 1e-6f) {
                v[0] = 0.0f;
                v[1] = 1.0f;
                v[2] = 0.0f;
                return;
            }
            const float inv = 1.0f / length;
            v[0] *= inv;
            v[1] *= inv;
            v[2] *= inv;
        }

        void blend_float_array(
            const float* a,
            const float* b,
            float* out,
            uint32_t count,
            float weight) noexcept
        {
            for (uint32_t i = 0; i < count; ++i) {
                out[i] = lerp(a[i], b[i], weight);
            }
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

        const TerrainVisualProxyLodRecord* nearest_lod(
            const TerrainVisualProxyChunkRecord& chunk,
            TerrainLodId lod_id) noexcept
        {
            const TerrainVisualProxyLodRecord* best = nullptr;
            uint32_t best_distance = (std::numeric_limits<uint32_t>::max)();
            for (const TerrainVisualProxyLodRecord& lod : chunk.lods) {
                const uint32_t distance =
                    lod.lod_id.value > lod_id.value
                        ? lod.lod_id.value - lod_id.value
                        : lod_id.value - lod.lod_id.value;
                if (!best || distance < best_distance) {
                    best = &lod;
                    best_distance = distance;
                }
            }
            return best;
        }

        void add_weighted_material_histogram(
            std::vector<TerrainMaterialCoverage>& out,
            const std::vector<TerrainMaterialCoverage>& in,
            float weight)
        {
            for (const TerrainMaterialCoverage& coverage : in) {
                auto it = std::find_if(
                    out.begin(),
                    out.end(),
                    [&](const TerrainMaterialCoverage& existing) {
                        return existing.material_id == coverage.material_id;
                    });
                if (it == out.end()) {
                    out.push_back(TerrainMaterialCoverage{
                        .material_id = coverage.material_id,
                        .coverage = coverage.coverage * weight,
                    });
                } else {
                    it->coverage += coverage.coverage * weight;
                }
            }
        }
    }

    TerrainVisualProxyPrefilterAggregates
    terrain_visual_proxy_resample_aggregates(
        const TerrainVisualProxyData& proxy,
        TerrainChunkId chunk_id,
        TerrainLodId target_lod)
    {
        TerrainVisualProxyPrefilterAggregates out{};
        const TerrainVisualProxyChunkRecord* chunk =
            find_chunk(proxy, chunk_id);
        if (!chunk) {
            return out;
        }
        const TerrainVisualProxyLodRecord* lod =
            nearest_lod(*chunk, target_lod);
        if (!lod) {
            return out;
        }
        out.source_region = lod->source_region_aggregate;
        out.lod_surface = lod->lod_surface_aggregate;
        out.lost_detail = lod->lost_detail_aggregate;
        return out;
    }

    TerrainVisualProxyPrefilterAggregates
    terrain_visual_proxy_blend_aggregates(
        const TerrainVisualProxyPrefilterAggregates& a,
        const TerrainVisualProxyPrefilterAggregates& b,
        float weight)
    {
        weight = clamp01(weight);
        TerrainVisualProxyPrefilterAggregates out{};
        out.source_region.normal_variance = lerp(
            a.source_region.normal_variance,
            b.source_region.normal_variance,
            weight);
        blend_float_array(
            a.source_region.height_range,
            b.source_region.height_range,
            out.source_region.height_range,
            2u,
            weight);
        out.source_region.roughness =
            lerp(a.source_region.roughness, b.source_region.roughness, weight);
        blend_float_array(
            a.source_region.dominant_normal,
            b.source_region.dominant_normal,
            out.source_region.dominant_normal,
            3u,
            weight);
        normalize_or_up(out.source_region.dominant_normal);

        out.lod_surface.normal_variance = lerp(
            a.lod_surface.normal_variance,
            b.lod_surface.normal_variance,
            weight);
        out.lod_surface.triangle_area_variance = lerp(
            a.lod_surface.triangle_area_variance,
            b.lod_surface.triangle_area_variance,
            weight);
        out.lod_surface.max_aspect_ratio = lerp(
            a.lod_surface.max_aspect_ratio,
            b.lod_surface.max_aspect_ratio,
            weight);
        blend_float_array(
            a.lod_surface.height_range,
            b.lod_surface.height_range,
            out.lod_surface.height_range,
            2u,
            weight);

        out.lost_detail.normal_variance = lerp(
            a.lost_detail.normal_variance,
            b.lost_detail.normal_variance,
            weight);
        out.lost_detail.height_detail = lerp(
            a.lost_detail.height_detail,
            b.lost_detail.height_detail,
            weight);

        add_weighted_material_histogram(
            out.source_region.material_histogram,
            a.source_region.material_histogram,
            1.0f - weight);
        add_weighted_material_histogram(
            out.source_region.material_histogram,
            b.source_region.material_histogram,
            weight);
        return out;
    }

    TerrainVisualProxyTable::TerrainVisualProxyTable()
    {
        slots_.push_back(Slot{});
    }

    wz::asset::ResourceHandle TerrainVisualProxyTable::add(
        TerrainVisualProxyData proxy)
    {
        Slot slot{};
        slot.epoch = 1;
        slot.occupied = true;
        slot.proxy = std::move(proxy);

        const uint32_t id = static_cast<uint32_t>(slots_.size());
        slots_.push_back(std::move(slot));

        return wz::asset::ResourceHandle{
            .id = id,
            .epoch = 1,
            .type = kAssetTypeTerrainVisualProxy,
        };
    }

    const TerrainVisualProxyData* TerrainVisualProxyTable::get(
        wz::asset::ResourceHandle handle) const
    {
        if (!handle.valid() || handle.id >= slots_.size()) {
            return nullptr;
        }

        const Slot& slot = slots_[handle.id];
        if (!slot.occupied || slot.epoch != handle.epoch) {
            return nullptr;
        }

        return &slot.proxy;
    }

    void TerrainVisualProxyTable::destroy()
    {
        slots_.clear();
        slots_.push_back(Slot{});
    }
}
