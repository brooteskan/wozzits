#pragma once

// engine/assets/key_factories/terrain.h

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/terrain/terrain.h>

#include <cstring>
#include <string_view>

namespace wz::engine::assets
{
    [[nodiscard]] inline uint64_t terrain_hash_float(float value) noexcept
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return static_cast<uint64_t>(bits);
    }

    [[nodiscard]] inline wz::asset::AssetKey make_terrain_from_height_field_key(
        std::string_view name,
        const wz::asset::AssetKey& height_field_key,
        float size_x,
        float size_z,
        float vertical_scale,
        float base_height,
        uint8_t render_mode,
        uint8_t collision_mode) noexcept
    {
        uint64_t h = detail::fnv1a_64(name);
        h = detail::mix64(h, terrain_hash_float(size_x));
        h = detail::mix64(h, terrain_hash_float(size_z));
        h = detail::mix64(h, terrain_hash_float(vertical_scale));
        h = detail::mix64(h, terrain_hash_float(base_height));
        h = detail::mix64(h, static_cast<uint64_t>(render_mode));
        h = detail::mix64(h, static_cast<uint64_t>(collision_mode));

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash =
                detail::hash_u64(kTerrainFromHeightFieldSchema.value),
            .compiler_hash =
                detail::hash_u64(kTerrainCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(height_field_key),
        };
    }

    [[nodiscard]] inline wz::asset::AssetKey make_terrain_from_mesh_key(
        std::string_view name,
        const wz::asset::AssetKey& mesh_key,
        uint8_t height_policy,
        float min_surface_normal_y,
        bool include_backfaces,
        uint8_t preferred_normal_source,
        uint8_t preferred_uv_source,
        uint8_t render_mode,
        uint8_t collision_mode) noexcept
    {
        uint64_t h = detail::fnv1a_64(name);
        h = detail::mix64(h, static_cast<uint64_t>(height_policy));
        h = detail::mix64(h, terrain_hash_float(min_surface_normal_y));
        h = detail::mix64(h, include_backfaces ? 1ull : 0ull);
        h = detail::mix64(h, static_cast<uint64_t>(preferred_normal_source));
        h = detail::mix64(h, static_cast<uint64_t>(preferred_uv_source));
        h = detail::mix64(h, static_cast<uint64_t>(render_mode));
        h = detail::mix64(h, static_cast<uint64_t>(collision_mode));

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(kTerrainFromMeshSchema.value),
            .compiler_hash = detail::hash_u64(kTerrainCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(mesh_key),
        };
    }
}
