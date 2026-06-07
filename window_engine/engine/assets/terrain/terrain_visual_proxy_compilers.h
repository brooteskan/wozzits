#pragma once

#include <asset/compiler.h>
#include <engine/assets/asset_cache_settings.h>
#include <engine/assets/terrain/terrain.h>
#include <engine/assets/terrain/terrain_visual_proxy.h>
#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    void register_terrain_visual_proxy_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        TerrainAssetTable& terrain_table,
        TerrainVisualProxyTable& terrain_visual_proxy_table,
        const EngineAssetCacheSettings& cache_settings);

    bool load_cached_terrain_visual_proxy(
        const EngineAssetCacheSettings& cache,
        const wz::asset::AssetKey& key,
        wz::Logger& logger,
        TerrainVisualProxyData& data);

    TerrainVisualProxyData compile_terrain_visual_proxy_for_tests(
        const wz::asset::AssetKey& proxy_key,
        const wz::asset::AssetKey& terrain_key,
        const TerrainAssetData& terrain);
}
