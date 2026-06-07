#pragma once

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>

#include <string_view>

namespace wz::engine::assets
{
    [[nodiscard]] inline wz::asset::AssetKey make_terrain_visual_proxy_key(
        std::string_view name,
        const wz::asset::AssetKey& terrain_key,
        const wz::asset::Hash& simplification_settings_hash = {}) noexcept
    {
        uint64_t h = detail::fnv1a_64(name);
        h = detail::mix64(h, simplification_settings_hash.lo);
        h = detail::mix64(h, simplification_settings_hash.hi);

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(kTerrainVisualProxySchema.value),
            .compiler_hash =
                detail::hash_u64(kTerrainVisualProxyCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(terrain_key),
        };
    }
}
