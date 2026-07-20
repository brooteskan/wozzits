#pragma once

// engine/assets/key_factories/atmosphere.h
//
// Key factory for Atmosphere recipe nodes.
//
// An Atmosphere is compiled directly from authored parameters with no
// dependency, so deps_hash is zero. Identity is derived entirely from the fog
// parameters (colour, density, start distance, height falloff, enabled) and the
// asset name.
//
// Every dial is folded, including fog_enabled: switching the fog off is a real
// authoring change that must re-key the atmosphere so its consumers rebuild,
// not a no-op just because density happens to be zero on both sides.
//
// Why include name?
//   Without name, two atmospheres with identical fog parameters produce the
//   same key and the second registration silently fails. Including name lets
//   callers author distinct atmospheres that share dials (same rationale as the
//   placement / procedural scalar field key factories).

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>

#include <cstring>
#include <string_view>

namespace wz::engine::assets
{
    [[nodiscard]] inline uint64_t atmosphere_hash_float(float value) noexcept
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return static_cast<uint64_t>(bits);
    }

    [[nodiscard]] inline wz::asset::AssetKey make_atmosphere_key(
        std::string_view name,
        const float fog_color[3],
        float fog_density,
        float fog_start_distance,
        float fog_height_falloff,
        bool fog_enabled,
        float fog_saturation) noexcept
    {
        uint64_t h = detail::fnv1a_64(name);
        h = detail::mix64(h, atmosphere_hash_float(fog_color[0]));
        h = detail::mix64(h, atmosphere_hash_float(fog_color[1]));
        h = detail::mix64(h, atmosphere_hash_float(fog_color[2]));
        h = detail::mix64(h, atmosphere_hash_float(fog_density));
        h = detail::mix64(h, atmosphere_hash_float(fog_start_distance));
        h = detail::mix64(h, atmosphere_hash_float(fog_height_falloff));
        h = detail::mix64(h, fog_enabled ? 1ull : 0ull);
        h = detail::mix64(h, atmosphere_hash_float(fog_saturation));

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(kAtmosphereSchema.value),
            .compiler_hash = detail::hash_u64(kAtmosphereCompilerVersion),
            .deps_hash = {},   // no dependency
        };
    }
}
