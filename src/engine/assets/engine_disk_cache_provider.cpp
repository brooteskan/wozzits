#include <engine/assets/engine_disk_cache_provider.h>

#include <engine/assets/collision/collision_compilers.h>
#include <engine/assets/disk_cache_keys.h>
#include <engine/assets/disk_cache_paths.h>
#include <engine/assets/mesh/mesh_compilers.h>
#include <engine/assets/scalar_field/scalar_field_compilers.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/terrain/terrain_compilers.h>
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
                    || schema == kScalarFieldProceduralSchema);
        }
    }

    EngineDiskCacheProvider::EngineDiskCacheProvider(
        const EngineAssetCacheSettings& cache_settings,
        wz::Logger& logger,
        ScalarFieldTable& scalar_fields,
        MeshTable& meshes,
        TerrainAssetTable& terrains,
        CollisionAssetTable& collisions)
        : cache_settings_(cache_settings)
        , logger_(logger)
        , scalar_fields_(scalar_fields)
        , meshes_(meshes)
        , terrains_(terrains)
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
        if (schema == kCollisionFromTerrainSchema
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

        if (schema == kCollisionFromTerrainSchema
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
