#pragma once

// engine/assets/key_factories/gaussian_splat_from_scalar_field.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

namespace wz::engine::assets {

    // Key for a splat cloud generated from a scalar field.
    // threshold, density, and radius are part of identity — different generation
    // parameters produce a different cloud even from the same source field.
    [[nodiscard]] inline wz::asset::AssetKey make_gaussian_splat_from_field_key(
        const wz::asset::AssetKey& scalar_field_key,
        float threshold, float density, float radius) noexcept
    {
        // Reinterpret floats as bits so we can mix them without precision concerns.
        uint32_t t_bits, d_bits, r_bits;
        static_assert(sizeof(float) == sizeof(uint32_t));
        std::memcpy(&t_bits, &threshold, sizeof(float));
        std::memcpy(&d_bits, &density, sizeof(float));
        std::memcpy(&r_bits, &radius, sizeof(float));

        const uint64_t params = detail::mix64(
            (static_cast<uint64_t>(t_bits) << 32) | d_bits,
            r_bits);

        return wz::asset::AssetKey{
            .content_hash = { params, 0 },
            .schema_hash = detail::hash_u64(kGaussianSplatFromFieldSchema.value),
            .compiler_hash = detail::hash_u64(kGaussianSplatCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(scalar_field_key),
        };
    }
}