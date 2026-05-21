#pragma once

// engine/assets/key_factories/gaussian_splat_terrain_surface.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/gaussian_splat/gaussian_splat.h>

#include <cstring>

namespace wz::engine::assets
{
    // Key for a terrain-surface splat cloud generated from a 2D height field.
    //
    // All compile parameters are part of identity — different parameters or
    // a different source field produce distinct keys, so cached output is
    // automatically invalidated by changes upstream.
    [[nodiscard]] inline wz::asset::AssetKey
        make_gaussian_splat_terrain_surface_from_height_field_key(
            const wz::asset::AssetKey& scalar_field_key,
            const GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc& desc
        ) noexcept
    {
        static_assert(sizeof(float) == sizeof(uint32_t));

        auto bits = [](float v) -> uint32_t {
            uint32_t out = 0;
            std::memcpy(&out, &v, sizeof(out));
            return out;
        };

        const uint64_t p0 = detail::mix64(
            (static_cast<uint64_t>(bits(desc.height_scale)) << 32) | bits(desc.step_x),
            (static_cast<uint64_t>(bits(desc.step_z)) << 32) | bits(desc.overlap_factor));

        const uint64_t p1 = detail::mix64(
            (static_cast<uint64_t>(bits(desc.thickness)) << 32) | desc.subsample_step,
            (static_cast<uint64_t>(bits(desc.opacity)) << 32) | bits(desc.flat_luminance));

        const uint64_t p2 = detail::mix64(
            static_cast<uint64_t>(bits(desc.steep_luminance)),
            0ull);

        const uint64_t content = detail::mix64(detail::mix64(p0, p1), p2);

        return wz::asset::AssetKey{
            .content_hash  = { content, 0 },
            .schema_hash   = detail::hash_u64(
                kGaussianSplatTerrainSurfaceFromHeightFieldSchema.value),
            .compiler_hash = detail::hash_u64(
                kGaussianSplatTerrainSurfaceFromHeightFieldCompilerVersion),
            .deps_hash     = detail::key_to_dep_hash(scalar_field_key),
        };
    }
}
