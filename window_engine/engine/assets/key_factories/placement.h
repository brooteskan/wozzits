#pragma once

// engine/assets/key_factories/placement.h
//
// Key factory for Placement recipe nodes.
//
// A Placement is compiled directly from authored parameters with no dependency,
// so deps_hash is zero. Identity is derived entirely from the frame parameters
// (origin, extent, base_height) and the asset name.
//
// Why include name?
//   Without name, two placements with identical origin/extent/base produce the
//   same key and the second registration silently fails. Including name lets
//   callers create distinct frames that share parameters without colliding in
//   the DAG (same rationale as the procedural scalar field key factory).

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>

#include <cstring>
#include <string_view>

namespace wz::engine::assets
{
    [[nodiscard]] inline uint64_t placement_hash_float(float value) noexcept
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return static_cast<uint64_t>(bits);
    }

    [[nodiscard]] inline wz::asset::AssetKey make_placement_key(
        std::string_view name,
        const float origin[3],
        const float extent[3],
        float base_height) noexcept
    {
        uint64_t h = detail::fnv1a_64(name);
        h = detail::mix64(h, placement_hash_float(origin[0]));
        h = detail::mix64(h, placement_hash_float(origin[1]));
        h = detail::mix64(h, placement_hash_float(origin[2]));
        h = detail::mix64(h, placement_hash_float(extent[0]));
        h = detail::mix64(h, placement_hash_float(extent[1]));
        h = detail::mix64(h, placement_hash_float(extent[2]));
        h = detail::mix64(h, placement_hash_float(base_height));

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(kPlacementSchema.value),
            .compiler_hash = detail::hash_u64(kPlacementCompilerVersion),
            .deps_hash = {},   // no dependency
        };
    }
}
