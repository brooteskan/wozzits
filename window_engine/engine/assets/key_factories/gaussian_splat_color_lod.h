#pragma once

// engine/assets/key_factories/gaussian_splat_color_lod.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/gaussian_splat/gaussian_splat_color_lod.h>

#include <cstring>

namespace wz::engine::assets
{
    // Key for a derived Gaussian splat color LOD product.
    //
    // Identity components:
    //   content_hash  — hash of the compile descriptor fields
    //   schema_hash   — kGaussianSplatColorLODSchema
    //   compiler_hash — kGaussianSplatColorLODCompilerVersion
    //   deps_hash     — derived from the source cloud's AssetKey
    //
    // Different compile parameters or a different source cloud produce a
    // distinct key, so cached output is automatically invalidated.

    [[nodiscard]] inline wz::asset::AssetKey make_gaussian_splat_color_lod_key(
        const wz::asset::AssetKey& source_cloud_key,
        const GaussianSplatColorLODCompileDesc& desc) noexcept
    {
        static_assert(sizeof(float) == sizeof(uint32_t));

        uint32_t r_bits, s_bits, v_bits;
        std::memcpy(&r_bits, &desc.neighbor_radius_world, sizeof(float));
        std::memcpy(&s_bits, &desc.gaussian_sigma_world,  sizeof(float));
        std::memcpy(&v_bits, &desc.color_variance_gain,   sizeof(float));

        const uint64_t flags =
            (desc.include_self       ? 1ull : 0ull) |
            (desc.use_opacity_weight ? 2ull : 0ull);

        const uint64_t p0 = detail::mix64(
            (static_cast<uint64_t>(r_bits) << 32) | s_bits,
            (static_cast<uint64_t>(v_bits) << 32) | flags);

        return wz::asset::AssetKey{
            .content_hash  = { p0, 0 },
            .schema_hash   = detail::hash_u64(kGaussianSplatColorLODSchema.value),
            .compiler_hash = detail::hash_u64(kGaussianSplatColorLODCompilerVersion),
            .deps_hash     = detail::key_to_dep_hash(source_cloud_key),
        };
    }
}
