#include <engine/assets/engine_disk_cache_provider.h>

#include <engine/assets/collision/collision_compilers.h>
#include <engine/assets/disk_cache_keys.h>
#include <engine/assets/disk_cache_paths.h>
#include <engine/assets/mesh/mesh_compilers.h>
#include <engine/assets/mesh_derived_field/mesh_derived_field_compilers.h>
#include <engine/assets/mesh_sparse_operator/mesh_sparse_operator_compilers.h>
#include <engine/assets/scalar_field/scalar_field_compilers.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/terrain/terrain_compilers.h>
#include <engine/assets/terrain/terrain_visual_proxy_compilers.h>
#include <engine/assets/type_extensions.h>

namespace wz::engine::assets
{
    namespace
    {
        bool is_scalar_field_cacheable(
            wz::asset::SchemaID schema,
            wz::asset::AssetType type)
        {
            return type == kAssetTypeScalarField
                && (schema == kScalarFieldFromRawF32Schema
                    // The Gaea .r32 heightfield: its compiler store_cached's the
                    // field, so the provider must serve it too, or a sealed cache
                    // with the .r32 stripped can't resolve it (issue #295). The
                    // store/serve sets must stay in lockstep.
                    || schema == kScalarFieldFromGaeaR32Schema
                    || schema == kScalarFieldProceduralSchema
                    // Worth caching more than the others: a 2048-square terrain
                    // is 4M samples through six octaves of gradient noise, all
                    // of it recomputed on every project open otherwise.
                    || schema == kScalarFieldTerrainSchema);
        }

        // The disk-cache key spec (subdirectory + seeds) for a cacheable
        // (schema, type), or nullptr when the type is not cache-backed. Single
        // source of truth for BOTH can_load (spec != null && entry on disk) and
        // is_cacheable (spec != null), so the served-type set can never drift
        // between the "is this cacheable" and "does the entry exist" questions.
        // Must stay in lockstep with the compilers' store_cached: a schema stored
        // but absent here is a sealed-cache miss with no source to fall back on.
        const internal::DiskCacheKeySpec* disk_cache_spec(
            wz::asset::SchemaID schema,
            wz::asset::AssetType type)
        {
            if (is_scalar_field_cacheable(schema, type)) {
                return &internal::kScalarFieldDiskCacheKey;
            }
            if (schema == kGLBMeshSchema && type == kAssetTypeMesh) {
                return &internal::kGLBMeshDiskCacheKey;
            }
            if (schema == kTerrainFromMeshSchema && type == kAssetTypeTerrain) {
                return &internal::kMeshTerrainDiskCacheKey;
            }
            if (schema == kMeshDerivedFieldExplicitSchema
                && type == kAssetTypeMeshDerivedField)
            {
                return &internal::kMeshDerivedFieldDiskCacheKey;
            }
            if (schema == kMeshWaveletAnalysisSchema
                && type == kAssetTypeMeshDerivedField)
            {
                return &internal::kMeshWaveletAnalysisDiskCacheKey;
            }
            if (schema == kMeshComputeDerivedFieldSchema
                && type == kAssetTypeMeshDerivedField)
            {
                return &internal::kMeshComputeDerivedFieldDiskCacheKey;
            }
            if (schema == kMeshSparseOperatorSchema
                && type == kAssetTypeMeshSparseOperator)
            {
                return &internal::kMeshSparseOperatorDiskCacheKey;
            }
            if (schema == kTerrainVisualProxySchema
                && type == kAssetTypeTerrainVisualProxy)
            {
                return &internal::kTerrainVisualProxyDiskCacheKey;
            }
            if ((schema == kCollisionFromTerrainSchema
                    || schema == kCollisionFromHeightFieldSchema)
                && type == kAssetTypeCollisionAsset)
            {
                return &internal::kCollisionTerrainDiskCacheKey;
            }
            return nullptr;
        }
    }

    EngineDiskCacheProvider::EngineDiskCacheProvider(
            const EngineAssetCacheSettings& cache_settings,
            wz::Logger& logger,
            ScalarFieldTable& scalar_fields,
            MeshTable& meshes,
            MeshDerivedFieldTable& mesh_derived_fields,
            MeshSparseOperatorTable& mesh_sparse_operators,
            TerrainAssetTable& terrains,
            TerrainVisualProxyTable& terrain_visual_proxies,
            CollisionAssetTable& collisions,
            wz::rhi::GpuResourceRegistry* gpu_resources,
            internal::RhiResourceTracker rhi_resource_tracker)
        : cache_settings_(cache_settings)
        , logger_(logger)
        , scalar_fields_(scalar_fields)
        , meshes_(meshes)
        , mesh_derived_fields_(mesh_derived_fields)
        , mesh_sparse_operators_(mesh_sparse_operators)
        , terrains_(terrains)
        , terrain_visual_proxies_(terrain_visual_proxies)
        , collisions_(collisions)
        , gpu_resources_(gpu_resources)
        , rhi_resource_tracker_(std::move(rhi_resource_tracker))
    {
    }

    bool EngineDiskCacheProvider::can_load(
        wz::asset::SchemaID schema,
        wz::asset::AssetType type,
        const wz::asset::AssetKey& key) const
    {
        if (!cache_settings_.enabled || cache_settings_.root.empty()) {
            return false;
        }

        const internal::DiskCacheKeySpec* spec = disk_cache_spec(schema, type);
        if (!spec) {
            return false;
        }
        return internal::disk_cache_asset_exists(
            cache_settings_,
            spec->subdirectory,
            key,
            spec->seed_lo,
            spec->seed_hi);
    }

    bool EngineDiskCacheProvider::is_cacheable(
        wz::asset::SchemaID schema,
        wz::asset::AssetType type) const
    {
        // Deliberately independent of enabled/root and of the on-disk entry: it
        // answers "would this type be served from the cache if present", which is
        // what the sealed-miss check needs. can_load layers the settings + entry
        // existence on top.
        return is_disk_cacheable(schema, type);
    }

    bool is_disk_cacheable(
        wz::asset::SchemaID schema,
        wz::asset::AssetType type)
    {
        return disk_cache_spec(schema, type) != nullptr;
    }

    std::optional<wz::asset::ResourceHandle> EngineDiskCacheProvider::load(
        wz::asset::SchemaID schema,
        wz::asset::AssetType type,
        const wz::asset::AssetKey& key)
    {
        if (is_scalar_field_cacheable(schema, type)) {
            ScalarFieldData data{};
            if (!internal::load_cached_scalar_field(
                    cache_settings_,
                    key,
                    logger_,
                    data))
            {
                return std::nullopt;
            }
            // Re-publish rhi residency (the compiler's finalize side effect) so a
            // cache-served height field is GPU-resident, not merely CPU-registered
            // — else the clipmap's scalar_field_texture binding is "not resident"
            // (issue #295). Uses `data` before the move into the table.
            if (gpu_resources_) {
                internal::publish_scalar_field_residency(
                    key, data, *gpu_resources_, rhi_resource_tracker_, logger_);
            }
            wz::asset::ResourceHandle handle =
                scalar_fields_.add(std::move(data));
            return handle.valid()
                ? std::optional<wz::asset::ResourceHandle>{ handle }
                : std::nullopt;
        }

        if (schema == kGLBMeshSchema && type == kAssetTypeMesh) {
            MeshData data{};
            if (!internal::load_cached_glb_mesh(
                    cache_settings_,
                    key,
                    logger_,
                    data))
            {
                return std::nullopt;
            }
            wz::asset::ResourceHandle handle = meshes_.add(std::move(data));
            return handle.valid()
                ? std::optional<wz::asset::ResourceHandle>{ handle }
                : std::nullopt;
        }

        if (schema == kTerrainFromMeshSchema && type == kAssetTypeTerrain) {
            TerrainAssetData data{};
            if (!internal::load_cached_mesh_terrain(
                    cache_settings_,
                    key,
                    logger_,
                    data))
            {
                return std::nullopt;
            }
            wz::asset::ResourceHandle handle = terrains_.add(std::move(data));
            return handle.valid()
                ? std::optional<wz::asset::ResourceHandle>{ handle }
                : std::nullopt;
        }

        if (schema == kMeshDerivedFieldExplicitSchema
            && type == kAssetTypeMeshDerivedField)
        {
            MeshDerivedFieldData data{};
            if (!internal::load_cached_mesh_derived_field(
                    cache_settings_,
                    key,
                    logger_,
                    data))
            {
                return std::nullopt;
            }
            wz::asset::ResourceHandle handle =
                mesh_derived_fields_.add(std::move(data));
            return handle.valid()
                ? std::optional<wz::asset::ResourceHandle>{ handle }
                : std::nullopt;
        }

        if (schema == kMeshWaveletAnalysisSchema
            && type == kAssetTypeMeshDerivedField)
        {
            MeshDerivedFieldData data{};
            if (!internal::load_cached_mesh_wavelet_analysis_field(
                    cache_settings_,
                    key,
                    logger_,
                    data))
            {
                return std::nullopt;
            }
            wz::asset::ResourceHandle handle =
                mesh_derived_fields_.add(std::move(data));
            return handle.valid()
                ? std::optional<wz::asset::ResourceHandle>{ handle }
                : std::nullopt;
        }

        if (schema == kMeshComputeDerivedFieldSchema
            && type == kAssetTypeMeshDerivedField)
        {
            MeshDerivedFieldData data{};
            if (!internal::load_cached_mesh_compute_derived_field(
                    cache_settings_,
                    key,
                    logger_,
                    data))
            {
                return std::nullopt;
            }
            wz::asset::ResourceHandle handle =
                mesh_derived_fields_.add(std::move(data));
            return handle.valid()
                ? std::optional<wz::asset::ResourceHandle>{ handle }
                : std::nullopt;
        }

        if (schema == kMeshSparseOperatorSchema
            && type == kAssetTypeMeshSparseOperator)
        {
            MeshSparseOperatorData data{};
            if (!internal::load_cached_mesh_sparse_operator(
                    cache_settings_,
                    key,
                    logger_,
                    data))
            {
                return std::nullopt;
            }
            wz::asset::ResourceHandle handle =
                mesh_sparse_operators_.add(std::move(data));
            return handle.valid()
                ? std::optional<wz::asset::ResourceHandle>{ handle }
                : std::nullopt;
        }

        if (schema == kTerrainVisualProxySchema
            && type == kAssetTypeTerrainVisualProxy)
        {
            TerrainVisualProxyData data{};
            if (!internal::load_cached_terrain_visual_proxy(
                    cache_settings_,
                    key,
                    logger_,
                    data))
            {
                return std::nullopt;
            }
            wz::asset::ResourceHandle handle =
                terrain_visual_proxies_.add(std::move(data));
            return handle.valid()
                ? std::optional<wz::asset::ResourceHandle>{ handle }
                : std::nullopt;
        }

        if ((schema == kCollisionFromTerrainSchema
                || schema == kCollisionFromHeightFieldSchema)
            && type == kAssetTypeCollisionAsset)
        {
            CollisionAssetData data{};
            if (!internal::load_cached_terrain_collision(
                    cache_settings_,
                    key,
                    logger_,
                    data))
            {
                return std::nullopt;
            }
            wz::asset::ResourceHandle handle = collisions_.add(std::move(data));
            return handle.valid()
                ? std::optional<wz::asset::ResourceHandle>{ handle }
                : std::nullopt;
        }

        return std::nullopt;
    }
}
