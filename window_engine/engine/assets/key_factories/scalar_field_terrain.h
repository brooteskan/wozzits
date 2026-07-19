#pragma once

// engine/assets/key_factories/scalar_field_terrain.h
//
// Key factory for procedural TERRAIN scalar field recipe nodes
// (kScalarFieldTerrainSchema).
//
// Like the procedural recipe next door, a terrain field has no file dependency,
// so deps_hash is zero and identity comes entirely from the recipe parameters
// plus the asset name. name is included for the same reason: without it two
// terrains authored with identical dials would produce the same key and the
// second registration would silently fail.
//
// EVERY dial must be folded in here. A dial left out of the key is worse than a
// dial that does nothing — nudging it in the editor re-resolves straight back to
// the cached old field, so the terrain visibly refuses to change and nothing in
// the log says why. The terrain_key_tests exist to hold that line.

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

#include <cstring>
#include <string_view>

namespace wz::engine::assets {

    // detail::float_bits comes from engine_asset_key_core.h — folding a float
    // dial in by its bit pattern keeps identity exact.

    [[nodiscard]] inline wz::asset::AssetKey make_terrain_scalar_field_key(
        std::string_view name,
        uint32_t         resolution,
        float            ridge_count,
        float            ridginess,
        float            roughness,
        uint32_t         detail_octaves,
        uint32_t         seed,
        float            basin_radius,
        float            basin_falloff,
        float            basin_depth,
        uint8_t          format_ordinal,
        uint8_t          domain_kind_ordinal) noexcept
    {
        // Three pairs of float dials, packed two-per-word.
        const uint64_t shape =
            (detail::float_bits(ridge_count) << 32) |
            detail::float_bits(ridginess);

        const uint64_t surface =
            (detail::float_bits(roughness) << 32) |
            static_cast<uint64_t>(detail_octaves);

        const uint64_t basin_a =
            (detail::float_bits(basin_radius) << 32) |
            detail::float_bits(basin_falloff);

        const uint64_t basin_b =
            (detail::float_bits(basin_depth) << 32) |
            static_cast<uint64_t>(seed);

        const uint64_t grid_and_kind =
            (static_cast<uint64_t>(resolution) << 32) |
            (static_cast<uint64_t>(format_ordinal) << 8) |
            static_cast<uint64_t>(domain_kind_ordinal);

        // name drives hi, as in the procedural factory, so names differing only
        // in late characters still perturb a different word than the dials do.
        const wz::asset::Hash name_hash = detail::hash_str(name);

        const uint64_t lo = detail::mix64(
            detail::mix64(
                detail::mix64(name_hash.lo, grid_and_kind),
                detail::mix64(shape, surface)),
            detail::mix64(basin_a, basin_b)
        );

        const uint64_t hi = detail::mix64(
            name_hash.hi,
            detail::mix64(grid_and_kind, basin_b)
        );

        return wz::asset::AssetKey{
            .content_hash = { lo, hi },
            .schema_hash = detail::hash_u64(kScalarFieldTerrainSchema.value),
            .compiler_hash =
                detail::hash_u64(kScalarFieldTerrainCompilerVersion),
            .deps_hash = {},   // no file dependency
        };
    }

} // namespace wz::engine::assets
