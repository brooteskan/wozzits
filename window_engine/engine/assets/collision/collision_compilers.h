#pragma once

// engine/assets/collision/collision_compilers.h

#include <asset/compiler.h>
#include <engine/assets/asset_cache_settings.h>
#include <engine/assets/collision/collision.h>
#include <engine/assets/mesh/mesh.h>
#include <engine/assets/terrain/terrain.h>

#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    void register_collision_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshTable& mesh_table,
        TerrainAssetTable& terrain_table,
        CollisionAssetTable& collision_table,
        const EngineAssetCacheSettings& cache_settings);
}
