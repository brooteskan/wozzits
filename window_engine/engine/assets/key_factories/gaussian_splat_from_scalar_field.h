#pragma once

// engine/assets/key_factories/gaussian_splat_from_scalar_field.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/gaussian_splat/gaussian_splat.h>

#include <cstring>

namespace wz::engine::assets {

    // Key for a splat cloud generated from a scalar field.
    // All generation parameters are part of identity — different parameters
    // produce a different cloud even from the same source field.
    // Field dimensions (width, height) are captured by deps_hash via the
    // scalar_field_key and are not repeated in the desc.
    [[nodiscard]] inline wz::asset::AssetKey make_gaussian_splat_from_scalar_field_key(
        const wz::asset::AssetKey& scalar_field_key,
        const GaussianSplatFromScalarFieldCompileDesc& desc) noexcept
    {
        static_assert(sizeof(float) == sizeof(uint32_t));

        uint32_t height_scale_bits, step_x_bits, step_z_bits, splat_scale_bits, opacity_bits, threshold_bits;
        std::memcpy(&height_scale_bits, &desc.height_scale,   sizeof(float));
        std::memcpy(&step_x_bits,       &desc.step_x,         sizeof(float));
        std::memcpy(&step_z_bits,       &desc.step_z,         sizeof(float));
        std::memcpy(&splat_scale_bits,  &desc.splat_scale,    sizeof(float));
        std::memcpy(&opacity_bits,      &desc.opacity,        sizeof(float));
        std::memcpy(&threshold_bits,    &desc.emit_threshold, sizeof(float));

        const uint64_t p0 = detail::mix64(
            (static_cast<uint64_t>(height_scale_bits) << 32) | step_x_bits,
            (static_cast<uint64_t>(step_z_bits) << 32) | splat_scale_bits
        );
        const uint64_t p1 = detail::mix64(
            (static_cast<uint64_t>(opacity_bits) << 32) | threshold_bits,
            (desc.normalize_values ? 1ull : 0ull) | (desc.use_threshold ? 2ull : 0ull)
        );
        // Fold subsample_step so clouds that differ only in subsampling get
        // distinct content-addressed keys (#208).
        const uint64_t p2 = detail::mix64(
            p1, static_cast<uint64_t>(desc.subsample_step));
        const uint64_t content = detail::mix64(p0, p2);

        return wz::asset::AssetKey{
            .content_hash  = { content, 0 },
            .schema_hash   = detail::hash_u64(kGaussianSplatFromFieldSchema.value),
            .compiler_hash = detail::hash_u64(kGaussianSplatFromScalarFieldCompilerVersion),
            .deps_hash     = detail::key_to_dep_hash(scalar_field_key),
        };
    }
}
