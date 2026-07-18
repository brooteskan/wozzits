#pragma once

// engine/assets/collision/collision_compilers.h

#include <asset/compiler.h>
#include <asset/system.h>
#include <engine/assets/asset_cache_settings.h>
#include <engine/assets/clipmap_lattice_schedule/clipmap_lattice_schedule.h>
#include <engine/assets/collision/collision.h>
#include <engine/assets/mesh/mesh.h>
#include <engine/assets/placement/placement.h>
#include <engine/assets/placed_field/placed_field.h>
#include <engine/assets/scalar_field/scalar_field.h>
#include <engine/assets/terrain/terrain.h>

#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    void register_collision_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        ScalarFieldTable& scalar_fields_table,
        MeshTable& mesh_table,
        TerrainAssetTable& terrain_table,
        CollisionAssetTable& collision_table,
        PlacementTable& placement_table,
        PlacedFieldTable& placed_field_table,
        // Resolves the OPTIONAL clipmap-lattice-schedule port, which supersedes
        // the authored render_lod_* params on the height-field recipe.
        ClipmapLatticeScheduleTable& clipmap_lattice_schedule_table,
        const wz::asset::AssetSystem* asset_system,
        const EngineAssetCacheSettings& cache_settings);

    bool load_cached_terrain_collision(
        const EngineAssetCacheSettings& cache,
        const wz::asset::AssetKey& key,
        wz::Logger& logger,
        CollisionAssetData& data);
}
