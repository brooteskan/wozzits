#pragma once

// engine/assets/key_factories/collision.h

#include <engine/assets/collision/collision.h>
#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>

#include <string_view>

namespace wz::engine::assets
{
    [[nodiscard]] inline uint64_t mix_collision_occupancy(
        uint64_t h,
        const CollisionOccupancyData& occupancy) noexcept
    {
        h = detail::mix64(h, static_cast<uint64_t>(occupancy.kind));
        h = detail::mix64(h, occupancy.blocks_movement ? 1ull : 0ull);
        h = detail::mix64(h, occupancy.queryable ? 1ull : 0ull);
        return h;
    }

    [[nodiscard]] inline wz::asset::AssetKey make_collision_from_mesh_key(
        std::string_view name,
        const wz::asset::AssetKey& mesh_key,
        CollisionBuildMethod build_method,
        const CollisionOccupancyData& occupancy) noexcept
    {
        uint64_t h = detail::fnv1a_64(name);
        h = detail::mix64(h, static_cast<uint64_t>(build_method));
        h = mix_collision_occupancy(h, occupancy);

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(kCollisionFromMeshSchema.value),
            .compiler_hash = detail::hash_u64(kCollisionCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(mesh_key),
        };
    }

    [[nodiscard]] inline wz::asset::AssetKey make_collision_from_terrain_key(
        std::string_view name,
        const wz::asset::AssetKey& terrain_key,
        CollisionBuildMethod build_method,
        const CollisionOccupancyData& occupancy,
        uint32_t projection_resolution_x = 0,
        uint32_t projection_resolution_y = 0) noexcept
    {
        uint64_t h = detail::fnv1a_64(name);
        h = detail::mix64(h, static_cast<uint64_t>(build_method));
        h = mix_collision_occupancy(h, occupancy);
        h = detail::mix64(h, projection_resolution_x);
        h = detail::mix64(h, projection_resolution_y);

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(kCollisionFromTerrainSchema.value),
            .compiler_hash = detail::hash_u64(kCollisionCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(terrain_key),
        };
    }
}
