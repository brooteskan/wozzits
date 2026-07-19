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
                    || schema == kScalarFieldProceduralSchema
                    // Worth caching more than the others: a 2048-square terrain
                    // is 4M samples through six octaves of gradient noise, all
                    // of it recomputed on every project open otherwise.
                    || schema == kScalarFieldTerrainSchema);
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
            CollisionAssetTable& collisions)
        : cache_settings_(cache_settings)
        , logger_(logger)
        , scalar_fields_(scalar_fields)
        , meshes_(meshes)
        , mesh_derived_fields_(mesh_derived_fields)
        , mesh_sparse_operators_(mesh_sparse_operators)
        , terrains_(terrains)
        , terrain_visual_proxies_(terrain_visual_proxies)
        , collisions_(collisions)
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

        if (is_scalar_field_cacheable(schema, type)) {
            return internal::disk_cache_asset_exists(
                cache_settings_,
                internal::kScalarFieldDiskCacheKey.subdirectory,
                key,
                internal::kScalarFieldDiskCacheKey.seed_lo,
                internal::kScalarFieldDiskCacheKey.seed_hi);
        }
        if (schema == kGLBMeshSchema && type == kAssetTypeMesh) {
            return internal::disk_cache_asset_exists(
                cache_settings_,
                internal::kGLBMeshDiskCacheKey.subdirectory,
                key,
                internal::kGLBMeshDiskCacheKey.seed_lo,
                internal::kGLBMeshDiskCacheKey.seed_hi);
        }
        if (schema == kTerrainFromMeshSchema && type == kAssetTypeTerrain) {
            return internal::disk_cache_asset_exists(
                cache_settings_,
                internal::kMeshTerrainDiskCacheKey.subdirectory,
                key,
                internal::kMeshTerrainDiskCacheKey.seed_lo,
                internal::kMeshTerrainDiskCacheKey.seed_hi);
        }
        if (schema == kMeshDerivedFieldExplicitSchema
            && type == kAssetTypeMeshDerivedField)
        {
            return internal::disk_cache_asset_exists(
                cache_settings_,
                internal::kMeshDerivedFieldDiskCacheKey.subdirectory,
                key,
                internal::kMeshDerivedFieldDiskCacheKey.seed_lo,
                internal::kMeshDerivedFieldDiskCacheKey.seed_hi);
        }
        if (schema == kMeshWaveletAnalysisSchema
            && type == kAssetTypeMeshDerivedField)
        {
            return internal::disk_cache_asset_exists(
                cache_settings_,
                internal::kMeshWaveletAnalysisDiskCacheKey.subdirectory,
                key,
                internal::kMeshWaveletAnalysisDiskCacheKey.seed_lo,
                internal::kMeshWaveletAnalysisDiskCacheKey.seed_hi);
        }
        if (schema == kMeshComputeDerivedFieldSchema
            && type == kAssetTypeMeshDerivedField)
        {
            return internal::disk_cache_asset_exists(
                cache_settings_,
                internal::kMeshComputeDerivedFieldDiskCacheKey.subdirectory,
                key,
                internal::kMeshComputeDerivedFieldDiskCacheKey.seed_lo,
                internal::kMeshComputeDerivedFieldDiskCacheKey.seed_hi);
        }
        if (schema == kMeshSparseOperatorSchema
            && type == kAssetTypeMeshSparseOperator)
        {
            return internal::disk_cache_asset_exists(
                cache_settings_,
                internal::kMeshSparseOperatorDiskCacheKey.subdirectory,
                key,
                internal::kMeshSparseOperatorDiskCacheKey.seed_lo,
                internal::kMeshSparseOperatorDiskCacheKey.seed_hi);
        }
        if (schema == kTerrainVisualProxySchema
            && type == kAssetTypeTerrainVisualProxy)
        {
            return internal::disk_cache_asset_exists(
                cache_settings_,
                internal::kTerrainVisualProxyDiskCacheKey.subdirectory,
                key,
                internal::kTerrainVisualProxyDiskCacheKey.seed_lo,
                internal::kTerrainVisualProxyDiskCacheKey.seed_hi);
        }
        if ((schema == kCollisionFromTerrainSchema
                || schema == kCollisionFromHeightFieldSchema)
            && type == kAssetTypeCollisionAsset)
        {
            return internal::disk_cache_asset_exists(
                cache_settings_,
                internal::kCollisionTerrainDiskCacheKey.subdirectory,
                key,
                internal::kCollisionTerrainDiskCacheKey.seed_lo,
                internal::kCollisionTerrainDiskCacheKey.seed_hi);
        }

        return false;
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
