#pragma once

#include <asset/compiler.h>
#include <logging/logger.h>
#include <engine/assets/asset_cache_settings.h>
#include <engine/assets/scalar_field/scalar_field.h>

namespace wz::engine::assets::internal {

    void register_scalar_field_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        ScalarFieldTable& scalar_field_table,
        const EngineAssetCacheSettings& cache_settings
    );

    bool load_cached_scalar_field(
        const EngineAssetCacheSettings& cache,
        const wz::asset::AssetKey& key,
        wz::Logger& logger,
        ScalarFieldData& field);

}
