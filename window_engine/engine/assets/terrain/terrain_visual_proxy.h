#pragma once

// engine/assets/terrain/terrain_visual_proxy.h
//
// CPU-side metadata for compiled terrain render representations.
// TerrainAssetData stays source/query/collision-adjacent; TerrainVisualProxyData
// names the immutable render proxy vocabulary used by LOD selection and later
// renderer/asset integrations.

#include <asset/types.h>

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace wz::engine::assets
{
    inline constexpr uint32_t kTerrainVisualProxySchemaVersion = 5;

    enum class TerrainVisualRepresentationKind : uint8_t
    {
        MeshChunks = 0,
        GridTiles,
        SurfelCloud,
    };

    // ─── Descriptor range check ───────────────────────────────────────────────────
    //
    // Stored in the visual-proxy disk cache as raw uint8 -- once for the proxy
    // and once per chunk -- and static_cast straight back with no range check,
    // so a flipped byte produced an out-of-range enum that every other check in
    // the loader missed (magic, format version, compiler version and the stored
    // key all cover different byte regions). It selects which of a chunk's
    // payloads is meaningful. Same shape as the scalar-field descriptors
    // (B1-C4).
    //
    // WHEN YOU APPEND AN ENUMERATOR, UPDATE THIS PREDICATE. It lives beside the
    // enum so the pairing is visible at the point of change. Under-accepting is
    // the safe direction: an entry carrying an enumerator this build does not
    // know is rejected, and the asset recompiles.

    [[nodiscard]] constexpr bool valid_terrain_visual_representation_kind(
        uint8_t v) noexcept
    {
        return v <= static_cast<uint8_t>(
            TerrainVisualRepresentationKind::SurfelCloud);
    }

    struct TerrainProxyId
    {
        wz::asset::AssetKey key{};

        [[nodiscard]] bool valid() const noexcept
        {
            return key != wz::asset::AssetKey{};
        }

        bool operator==(const TerrainProxyId&) const = default;
    };

    struct TerrainChunkId
    {
        uint32_t value = 0;

        bool operator==(const TerrainChunkId&) const = default;
    };

    inline constexpr TerrainChunkId kInvalidTerrainChunkId{
        (std::numeric_limits<uint32_t>::max)()
    };

    struct TerrainLodId
    {
        uint32_t value = 0;

        bool operator==(const TerrainLodId&) const = default;
    };

    struct TerrainRepresentationId
    {
        uint32_t value = 0;

        bool operator==(const TerrainRepresentationId&) const = default;
    };

    enum class TerrainVisualProxyBoundaryEdge : uint8_t
    {
        NegativeX,
        PositiveX,
        NegativeZ,
        PositiveZ,
    };

    struct TerrainVisualProxyBounds
    {
        float min[3]{ 0.0f, 0.0f, 0.0f };
        float max[3]{ 0.0f, 0.0f, 0.0f };

        [[nodiscard]] bool valid() const noexcept
        {
            return min[0] <= max[0]
                && min[1] <= max[1]
                && min[2] <= max[2];
        }
    };

    struct TerrainMaterialCoverage
    {
        uint32_t material_id = 0;
        float coverage = 0.0f;
    };

    struct TerrainVisualProxyAggregate
    {
        float mean_height = 0.0f;
        float height_variance = 0.0f;
        float normal_mean[3]{ 0.0f, 1.0f, 0.0f };
        float normal_variance[2]{ 0.0f, 0.0f };
        float albedo_mean[3]{ 1.0f, 1.0f, 1.0f };
        std::vector<TerrainMaterialCoverage> material_coverage;
    };

    struct TerrainVisualProxySourceRegionAggregate
    {
        float normal_variance = 0.0f;
        float height_range[2]{ 0.0f, 0.0f };
        float roughness = 0.0f;
        float dominant_normal[3]{ 0.0f, 1.0f, 0.0f };
        std::vector<TerrainMaterialCoverage> material_histogram;
    };

    struct TerrainVisualProxyLodSurfaceAggregate
    {
        float normal_variance = 0.0f;
        float triangle_area_variance = 0.0f;
        float max_aspect_ratio = 0.0f;
        float height_range[2]{ 0.0f, 0.0f };
    };

    struct TerrainVisualProxyLostDetailAggregate
    {
        float normal_variance = 0.0f;
        float height_detail = 0.0f;
    };

    struct TerrainVisualProxyPrefilterAggregates
    {
        TerrainVisualProxySourceRegionAggregate source_region{};
        TerrainVisualProxyLodSurfaceAggregate lod_surface{};
        TerrainVisualProxyLostDetailAggregate lost_detail{};
    };

    enum TerrainVisualChunkBoundaryFlags : uint32_t
    {
        TerrainVisualChunkBoundary_None = 0,
        TerrainVisualChunkBoundary_NegativeX = 1u << 0u,
        TerrainVisualChunkBoundary_PositiveX = 1u << 1u,
        TerrainVisualChunkBoundary_NegativeZ = 1u << 2u,
        TerrainVisualChunkBoundary_PositiveZ = 1u << 3u,
    };

    struct TerrainVisualChunkBoundaryMetadata
    {
        uint32_t boundary_flags = TerrainVisualChunkBoundary_None;
        TerrainChunkId negative_x_neighbor = kInvalidTerrainChunkId;
        TerrainChunkId positive_x_neighbor = kInvalidTerrainChunkId;
        TerrainChunkId negative_z_neighbor = kInvalidTerrainChunkId;
        TerrainChunkId positive_z_neighbor = kInvalidTerrainChunkId;
    };

    struct TerrainVisualProxyBoundaryPoint
    {
        float position[3]{ 0.0f, 0.0f, 0.0f };
    };

    struct TerrainVisualProxyLodBoundaryRing
    {
        std::vector<TerrainVisualProxyBoundaryPoint> negative_x;
        std::vector<TerrainVisualProxyBoundaryPoint> positive_x;
        std::vector<TerrainVisualProxyBoundaryPoint> negative_z;
        std::vector<TerrainVisualProxyBoundaryPoint> positive_z;

        [[nodiscard]] uint32_t point_count() const noexcept
        {
            return static_cast<uint32_t>(
                negative_x.size()
                + positive_x.size()
                + negative_z.size()
                + positive_z.size());
        }
    };

    struct TerrainVisualProxyTransitionVertex
    {
        float position[3]{ 0.0f, 0.0f, 0.0f };
        // 0 = owning chunk edge, 1 = neighboring chunk edge.
        uint8_t side = 0u;
    };

    struct TerrainVisualProxyTransitionStrip
    {
        TerrainChunkId chunk_id{};
        TerrainChunkId neighbor_chunk_id{};
        TerrainLodId lod_id{};
        TerrainLodId neighbor_lod_id{};
        TerrainVisualProxyBoundaryEdge edge =
            TerrainVisualProxyBoundaryEdge::NegativeX;
        std::vector<TerrainVisualProxyTransitionVertex> vertices;
        std::vector<uint32_t> indices;

        [[nodiscard]] uint32_t triangle_count() const noexcept
        {
            return static_cast<uint32_t>(indices.size() / 3u);
        }

        [[nodiscard]] bool valid() const noexcept
        {
            if (chunk_id == neighbor_chunk_id
                || lod_id == neighbor_lod_id
                || vertices.size() < 3u
                || indices.size() < 3u
                || indices.size() % 3u != 0u)
            {
                return false;
            }
            for (const TerrainVisualProxyTransitionVertex& vertex :
                 vertices)
            {
                if (vertex.side > 1u) {
                    return false;
                }
            }
            for (uint32_t index : indices) {
                if (index >= vertices.size()) {
                    return false;
                }
            }
            return true;
        }
    };

    struct TerrainVisualProxySurfel
    {
        float position[3]{ 0.0f, 0.0f, 0.0f };
        float normal[3]{ 0.0f, 1.0f, 0.0f };
        float radius = 0.0f;
        float albedo[3]{ 1.0f, 1.0f, 1.0f };
        float roughness = 0.0f;
        uint32_t material_id = 0u;

        [[nodiscard]] bool valid() const noexcept
        {
            return radius > 0.0f && roughness >= 0.0f;
        }
    };

    struct TerrainVisualProxySurfelDensityLevel
    {
        TerrainLodId density_id{};
        float spacing = 0.0f;
        float representative_radius = 0.0f;
        uint32_t first_surfel = 0u;
        uint32_t surfel_count = 0u;
        uint32_t equivalent_triangle_cost = 0u;
        TerrainVisualProxyBounds bounds{};

        [[nodiscard]] bool valid() const noexcept
        {
            return spacing > 0.0f
                && representative_radius > 0.0f
                && surfel_count > 0u
                && equivalent_triangle_cost > 0u
                && bounds.valid();
        }
    };

    struct TerrainVisualProxyLodRecord
    {
        TerrainLodId lod_id{};
        TerrainRepresentationId representation_id{};
        TerrainVisualRepresentationKind representation_kind =
            TerrainVisualRepresentationKind::MeshChunks;

        TerrainVisualProxyBounds bounds{};
        uint32_t first_index = 0;
        uint32_t index_count = 0;
        uint32_t first_vertex = 0;
        uint32_t vertex_count = 0;
        uint32_t triangle_count = 0;
        float conservative_geometric_error = 0.0f;

        // Optional CPU asset reference for representation data. GPU resources
        // remain backend-owned and are intentionally not required here.
        wz::asset::AssetKey mesh_asset{};

        TerrainVisualProxySourceRegionAggregate source_region_aggregate{};
        TerrainVisualProxyLodSurfaceAggregate lod_surface_aggregate{};
        TerrainVisualProxyLostDetailAggregate lost_detail_aggregate{};
        TerrainVisualProxyLodBoundaryRing boundary_ring{};

        [[nodiscard]] bool valid() const noexcept
        {
            return bounds.valid()
                && triangle_count > 0
                && conservative_geometric_error >= 0.0f;
        }
    };

    struct TerrainVisualProxyChunkRecord
    {
        TerrainChunkId chunk_id{};
        TerrainRepresentationId representation_id{};
        TerrainVisualRepresentationKind representation_kind =
            TerrainVisualRepresentationKind::MeshChunks;

        TerrainVisualProxyBounds bounds{};
        uint32_t first_triangle = 0;
        uint32_t triangle_count = 0;
        uint32_t first_vertex = 0;
        uint32_t vertex_count = 0;

        TerrainVisualProxyAggregate aggregate{};
        TerrainVisualChunkBoundaryMetadata boundary{};
        std::vector<TerrainVisualProxySurfelDensityLevel>
            surfel_density_levels;
        std::vector<TerrainVisualProxySurfel> surfels;
        std::vector<TerrainVisualProxyLodRecord> lods;
        std::vector<TerrainVisualProxyTransitionStrip> transition_strips;

        [[nodiscard]] bool valid() const noexcept
        {
            if (!bounds.valid()
                || triangle_count == 0
                || vertex_count == 0
                || lods.empty())
            {
                return false;
            }

            for (const TerrainVisualProxyTransitionStrip& strip :
                 transition_strips)
            {
                if (!strip.valid()) {
                    return false;
                }
            }

            for (const TerrainVisualProxySurfelDensityLevel& level :
                 surfel_density_levels)
            {
                if (!level.valid()
                    // Same wrapping sum as the decoder and the consumer
                    // (issue #310, A4-C27). All three carried it, so there was
                    // no defence in depth -- a wrapped range passed every gate.
                    || static_cast<std::uint64_t>(level.first_surfel)
                           + static_cast<std::uint64_t>(level.surfel_count)
                        > surfels.size())
                {
                    return false;
                }
            }
            for (const TerrainVisualProxySurfel& surfel : surfels) {
                if (!surfel.valid()) {
                    return false;
                }
            }

            return true;
        }
    };

    struct TerrainVisualProxyData
    {
        uint32_t schema_version = kTerrainVisualProxySchemaVersion;
        uint32_t compiler_version = 0;
        wz::asset::AssetKey source_asset_key{};
        wz::asset::Hash simplification_settings_hash{};

        TerrainProxyId terrain_proxy_id{};
        TerrainVisualRepresentationKind primary_representation_kind =
            TerrainVisualRepresentationKind::MeshChunks;
        TerrainVisualProxyBounds bounds{};
        std::vector<TerrainVisualProxyChunkRecord> chunks;

        [[nodiscard]] bool valid() const noexcept
        {
            if (schema_version != kTerrainVisualProxySchemaVersion
                || compiler_version == 0
                || source_asset_key == wz::asset::AssetKey{}
                || !terrain_proxy_id.valid()
                || !bounds.valid()
                || chunks.empty())
            {
                return false;
            }

            for (const TerrainVisualProxyChunkRecord& chunk : chunks) {
                if (!chunk.valid()) {
                    return false;
                }
                for (const TerrainVisualProxyLodRecord& lod : chunk.lods) {
                    if (!lod.valid()) {
                        return false;
                    }
                }
            }

            return true;
        }

        [[nodiscard]] uint32_t chunk_count() const noexcept
        {
            return static_cast<uint32_t>(chunks.size());
        }

        [[nodiscard]] uint32_t lod_record_count() const noexcept
        {
            uint32_t count = 0;
            for (const TerrainVisualProxyChunkRecord& chunk : chunks) {
                count += static_cast<uint32_t>(chunk.lods.size());
            }
            return count;
        }

        [[nodiscard]] uint32_t transition_strip_count() const noexcept
        {
            uint32_t count = 0;
            for (const TerrainVisualProxyChunkRecord& chunk : chunks) {
                count +=
                    static_cast<uint32_t>(chunk.transition_strips.size());
            }
            return count;
        }

        [[nodiscard]] uint32_t surfel_count() const noexcept
        {
            uint32_t count = 0;
            for (const TerrainVisualProxyChunkRecord& chunk : chunks) {
                count += static_cast<uint32_t>(chunk.surfels.size());
            }
            return count;
        }
    };

    [[nodiscard]] TerrainVisualProxyPrefilterAggregates
    terrain_visual_proxy_resample_aggregates(
        const TerrainVisualProxyData& proxy,
        TerrainChunkId chunk_id,
        TerrainLodId target_lod);

    [[nodiscard]] TerrainVisualProxyPrefilterAggregates
    terrain_visual_proxy_blend_aggregates(
        const TerrainVisualProxyPrefilterAggregates& a,
        const TerrainVisualProxyPrefilterAggregates& b,
        float weight);

    class TerrainVisualProxyTable
    {
    public:
        TerrainVisualProxyTable();

        wz::asset::ResourceHandle add(TerrainVisualProxyData proxy);

        const TerrainVisualProxyData* get(
            wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        struct Slot
        {
            uint32_t epoch = 0;
            bool occupied = false;
            TerrainVisualProxyData proxy;
        };

        std::vector<Slot> slots_;
    };

    struct TerrainRenderable
    {
        wz::asset::AssetKey terrain_asset{};
        wz::asset::AssetKey visual_proxy_asset{};

        [[nodiscard]] bool valid() const noexcept
        {
            return terrain_asset != wz::asset::AssetKey{};
        }
    };
}
