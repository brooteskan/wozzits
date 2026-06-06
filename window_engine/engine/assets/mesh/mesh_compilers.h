#pragma once

#include <asset/compiler.h>
#include <engine/assets/asset_cache_settings.h>
#include <logging/logger.h>
#include <engine/assets/mesh/mesh.h>

namespace wz::engine::assets::internal {

    void register_mesh_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshTable& mesh_table,
        const EngineAssetCacheSettings& cache_settings
    );

    bool load_cached_glb_mesh(
        const EngineAssetCacheSettings& cache,
        const wz::asset::AssetKey& key,
        wz::Logger& logger,
        MeshData& mesh);

}
