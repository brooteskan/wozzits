#pragma once

// engine/assets/terrain/terrain_compilers.h

#include <asset/compiler.h>
#include <engine/assets/asset_cache_settings.h>
#include <engine/assets/mesh/mesh.h>
#include <engine/assets/scalar_field/scalar_field.h>
#include <engine/assets/terrain/terrain.h>

#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    void register_terrain_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        ScalarFieldTable& scalar_fields_table,
        MeshTable& mesh_table,
        TerrainAssetTable& terrain_table,
        const EngineAssetCacheSettings& cache_settings);
}
