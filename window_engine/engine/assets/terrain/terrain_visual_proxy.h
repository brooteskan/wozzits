#pragma once

// engine/assets/terrain/terrain_visual_proxy.h
//
// CPU-side metadata for compiled terrain render representations.
// TerrainAssetData stays source/query/collision-adjacent; TerrainVisualProxyData
// names the immutable render proxy vocabulary used by LOD selection and later
// renderer/asset integrations.

#include <asset/types.h>

#include <cstdint>
#include <vector>

namespace wz::engine::assets
{
    inline constexpr uint32_t kTerrainVisualProxySchemaVersion = 1;

    enum class TerrainVisualRepresentationKind : uint8_t
    {
        MeshChunks = 0,
        GridTiles,
        SurfelCloud,
    };

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
        TerrainChunkId negative_x_neighbor{};
        TerrainChunkId positive_x_neighbor{};
        TerrainChunkId negative_z_neighbor{};
        TerrainChunkId positive_z_neighbor{};
    };

    struct TerrainVisualProxyLodRecord
    {
        TerrainLodId lod_id{};
        TerrainRepresentationId representation_id{};
        TerrainVisualRepresentationKind representation_kind =
            TerrainVisualRepresentationKind::MeshChunks;

        uint32_t first_index = 0;
        uint32_t index_count = 0;
        uint32_t first_vertex = 0;
        uint32_t vertex_count = 0;
        uint32_t triangle_count = 0;
        float conservative_geometric_error = 0.0f;

        // Optional CPU asset reference for representation data. GPU resources
        // remain backend-owned and are intentionally not required here.
        wz::asset::AssetKey mesh_asset{};

        TerrainVisualProxyAggregate source_region_aggregate{};
        TerrainVisualProxyAggregate lod_surface_aggregate{};

        [[nodiscard]] bool valid() const noexcept
        {
            return triangle_count > 0
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
        std::vector<TerrainVisualProxyLodRecord> lods;

        [[nodiscard]] bool valid() const noexcept
        {
            return bounds.valid()
                && triangle_count > 0
                && vertex_count > 0
                && !lods.empty();
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
