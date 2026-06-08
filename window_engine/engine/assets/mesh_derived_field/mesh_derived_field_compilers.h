#pragma once

// engine/assets/mesh_derived_field/mesh_derived_field_compilers.h

#include <asset/compiler.h>
#include <engine/assets/asset_cache_settings.h>
#include <engine/assets/mesh/mesh.h>
#include <engine/assets/mesh_derived_field/mesh_derived_field.h>
#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    void register_mesh_derived_field_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshTable& mesh_table,
        MeshDerivedFieldTable& mesh_derived_field_table,
        const EngineAssetCacheSettings& cache_settings);

    bool load_cached_mesh_derived_field(
        const EngineAssetCacheSettings& cache,
        const wz::asset::AssetKey& key,
        wz::Logger& logger,
        MeshDerivedFieldData& field);
}
