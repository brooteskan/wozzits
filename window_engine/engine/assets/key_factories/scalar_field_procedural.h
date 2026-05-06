#pragma once

// engine/assets/key_factories/scalar_field_procedural.h
//
// Key factory for procedural scalar field recipe nodes.
//
// Unlike file-backed scalar fields, procedural nodes have no file dependency,
// so deps_hash is zero. Identity is derived entirely from the recipe parameters
// and the asset name.
//
// Why include name?
//   Without name, two procedural fields with identical dimensions/generator/
//   parameters produce the same key and the second registration silently fails.
//   Including name lets callers create distinct debug/test assets that share
//   generation parameters without colliding in the DAG.

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

#include <cstring>
#include <string_view>

namespace wz::engine::assets {

    [[nodiscard]] inline wz::asset::AssetKey make_procedural_scalar_field_key(
        std::string_view name,
        uint32_t         width,
        uint32_t         height,
        uint32_t         depth,
        uint8_t          generator_ordinal,
        float            frequency,
        float            amplitude,
        uint8_t          format_ordinal,
        uint8_t          domain_kind_ordinal) noexcept
    {
        // Reinterpret floats as bits for stable mixing without precision concerns.
        uint32_t frequency_bits = 0;
        uint32_t amplitude_bits = 0;
        static_assert(sizeof(float) == sizeof(uint32_t));
        std::memcpy(&frequency_bits, &frequency, sizeof(float));
        std::memcpy(&amplitude_bits, &amplitude, sizeof(float));

        // Pack spatial identity into two 64-bit words.
        const uint64_t dims =
            (static_cast<uint64_t>(width) << 32) |
            static_cast<uint64_t>(height);

        const uint64_t depth_and_generator =
            (static_cast<uint64_t>(depth) << 8) |
            static_cast<uint64_t>(generator_ordinal);

        // Pack generation parameters.
        const uint64_t float_params =
            (static_cast<uint64_t>(frequency_bits) << 32) |
            static_cast<uint64_t>(amplitude_bits);

        const uint64_t format_and_domain =
            (static_cast<uint64_t>(format_ordinal) << 8) |
            static_cast<uint64_t>(domain_kind_ordinal);

        // Mix all recipe parameters together with the name hash.
        // name drives hi so that names differing only in late characters still
        // affect a different word than the numeric parameters in lo.
        const wz::asset::Hash name_hash = detail::hash_str(name);

        const uint64_t lo = detail::mix64(
            detail::mix64(name_hash.lo, dims),
            detail::mix64(depth_and_generator, float_params)
        );

        const uint64_t hi = detail::mix64(
            name_hash.hi,
            format_and_domain
        );

        return wz::asset::AssetKey{
            .content_hash = { lo, hi },
            .schema_hash = detail::hash_u64(kScalarFieldProceduralSchema.value),
            .compiler_hash = detail::hash_u64(kScalarFieldCompilerVersion),
            .deps_hash = {},   // no file dependency
        };
    }

} // namespace wz::engine::assets
