#pragma once

#include <asset/external_cache_provider.h>
#include <engine/assets/asset_cache_settings.h>
#include <engine/assets/collision/collision.h>
#include <engine/assets/mesh/mesh.h>
#include <engine/assets/scalar_field/scalar_field.h>
#include <engine/assets/terrain/terrain.h>
#include <logging/logger.h>

#include <optional>

namespace wz::engine::assets
{
    class EngineDiskCacheProvider final : public wz::asset::ExternalCacheProvider
    {
    public:
        EngineDiskCacheProvider(
            const EngineAssetCacheSettings& cache_settings,
            wz::Logger& logger,
            ScalarFieldTable& scalar_fields,
            MeshTable& meshes,
            TerrainAssetTable& terrains,
            CollisionAssetTable& collisions);

        bool can_load(
            wz::asset::SchemaID schema,
            wz::asset::AssetType type,
            const wz::asset::AssetKey& key) const override;

        std::optional<wz::asset::ResourceHandle> load(
            wz::asset::SchemaID schema,
            wz::asset::AssetType type,
            const wz::asset::AssetKey& key) override;

    private:
        const EngineAssetCacheSettings& cache_settings_;
        wz::Logger& logger_;
        ScalarFieldTable& scalar_fields_;
        MeshTable& meshes_;
        TerrainAssetTable& terrains_;
        CollisionAssetTable& collisions_;
    };
}
