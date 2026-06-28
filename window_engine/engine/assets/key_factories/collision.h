#pragma once

// engine/assets/key_factories/collision.h

#include <engine/assets/collision/collision.h>
#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>

#include <cstring>
#include <string_view>

namespace wz::engine::assets
{
    [[nodiscard]] inline uint64_t collision_hash_float(float value) noexcept
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return static_cast<uint64_t>(bits);
    }

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

    // deps_hash folds the scalar field dep, then (if present) the OPTIONAL
    // placement dep — in the SAME order the module registers the graph edges
    // (scalar field first, placement second). combine_dep_hashes is not
    // commutative, so this ordering must match the registration order. Folding
    // the placement here is what makes a placement change re-compile the
    // collision (issue #218 Phase 1). A null/default placement key reproduces
    // the original single-dep key exactly, so back-compat is preserved.
    [[nodiscard]] inline wz::asset::AssetKey
    make_collision_from_height_field_key(
        std::string_view name,
        const wz::asset::AssetKey& scalar_field_key,
        const float origin[2],
        const float size[2],
        float vertical_scale,
        float base_height,
        const CollisionOccupancyData& occupancy,
        uint32_t projection_resolution_x = 0,
        uint32_t projection_resolution_y = 0,
        const wz::asset::AssetKey& placement_key = {}) noexcept
    {
        uint64_t h = detail::fnv1a_64(name);
        h = detail::mix64(h, collision_hash_float(origin[0]));
        h = detail::mix64(h, collision_hash_float(origin[1]));
        h = detail::mix64(h, collision_hash_float(size[0]));
        h = detail::mix64(h, collision_hash_float(size[1]));
        h = detail::mix64(h, collision_hash_float(vertical_scale));
        h = detail::mix64(h, collision_hash_float(base_height));
        h = mix_collision_occupancy(h, occupancy);
        h = detail::mix64(h, projection_resolution_x);
        h = detail::mix64(h, projection_resolution_y);

        wz::asset::Hash deps_hash = detail::key_to_dep_hash(scalar_field_key);
        if (!(placement_key == wz::asset::AssetKey{})) {
            deps_hash = detail::combine_dep_hashes(
                deps_hash,
                detail::key_to_dep_hash(placement_key));
        }

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash =
                detail::hash_u64(kCollisionFromHeightFieldSchema.value),
            .compiler_hash = detail::hash_u64(kCollisionCompilerVersion),
            .deps_hash = deps_hash,
        };
    }
}
