#pragma once

// engine/assets/key_factories/gaussian_splat.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

#include <cstdint>
#include <cstring>
#include <string_view>

namespace wz::engine::assets
{
    namespace make_procedural_gaussian_splat_cloud_key_detail
    {
        [[nodiscard]] inline uint32_t float_to_u32_bits(float v) noexcept
        {
            uint32_t bits = 0;
            static_assert(sizeof(bits) == sizeof(v));
            std::memcpy(&bits, &v, sizeof(bits));
            return bits;
        }
    }

    [[nodiscard]] inline wz::asset::AssetKey make_procedural_gaussian_splat_cloud_key(
        std::string_view name,
        uint32_t count,
        float radius,
        float splat_scale) noexcept
    {
        uint64_t h = detail::fnv1a_64(name);
        h = detail::mix64(h, static_cast<uint64_t>(count));
        h = detail::mix64(h, static_cast<uint64_t>(make_procedural_gaussian_splat_cloud_key_detail::float_to_u32_bits(radius)));
        h = detail::mix64(h, static_cast<uint64_t>(make_procedural_gaussian_splat_cloud_key_detail::float_to_u32_bits(splat_scale)));

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(kProceduralGaussianSplatCloudSchema.value),
            .compiler_hash = detail::hash_u64(kProceduralGaussianSplatCloudCompilerVersion),
            .deps_hash = {},
        };
    }
}