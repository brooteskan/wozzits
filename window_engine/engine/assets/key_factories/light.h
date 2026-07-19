#pragma once

// engine/assets/key_factories/light.h

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/light/light.h>
#include <engine/assets/schema_ids.h>

#include <cstring>
#include <string_view>

namespace wz::engine::assets
{
    namespace detail
    {
        // float_bits now lives in engine_asset_key_core.h — every key factory
        // with float dials needs it, and a second inline definition here breaks
        // any TU that includes both this header and another factory's.

        [[nodiscard]] inline uint64_t mix_float3(
            float x,
            float y,
            float z) noexcept
        {
            return mix64(mix64(float_bits(x), float_bits(y)), float_bits(z));
        }
    }

    [[nodiscard]] inline wz::asset::AssetKey make_direct_light_key(
        std::string_view name,
        const DirectLightCompileDesc& desc) noexcept
    {
        const wz::asset::Hash name_hash = detail::hash_str(name);
        const uint64_t color_hash = detail::mix_float3(
            desc.color[0],
            desc.color[1],
            desc.color[2]);
        const uint64_t params = detail::mix64(
            detail::mix64(
                static_cast<uint64_t>(desc.kind),
                detail::float_bits(desc.intensity)),
            detail::mix64(
                detail::float_bits(desc.range),
                detail::mix64(
                    detail::float_bits(desc.inner_cone_radians),
                    detail::float_bits(desc.outer_cone_radians))));

        return wz::asset::AssetKey{
            .content_hash = {
                detail::mix64(name_hash.lo, color_hash),
                detail::mix64(name_hash.hi, params),
            },
            .schema_hash = detail::hash_u64(kDirectLightSchema.value),
            .compiler_hash = detail::hash_u64(kDirectLightCompilerVersion),
            .deps_hash = {},
        };
    }

    [[nodiscard]] inline wz::asset::AssetKey make_ambient_lighting_key(
        std::string_view name,
        const AmbientLightingCompileDesc& desc) noexcept
    {
        const wz::asset::Hash name_hash = detail::hash_str(name);
        const uint64_t color_hash = detail::mix_float3(
            desc.color[0],
            desc.color[1],
            desc.color[2]);
        const uint64_t mode_params = detail::mix64(
            static_cast<uint64_t>(desc.mode),
            detail::mix64(
                detail::float_bits(desc.intensity),
                static_cast<uint64_t>(desc.domain_mapping)));

        wz::asset::Hash deps{};
        if (!(desc.intensity_field == wz::asset::AssetKey{})) {
            deps = detail::combine_dep_hashes(
                deps,
                detail::key_to_dep_hash(desc.intensity_field));
        }
        if (!(desc.color_field == wz::asset::AssetKey{})) {
            deps = detail::combine_dep_hashes(
                deps,
                detail::key_to_dep_hash(desc.color_field));
        }

        return wz::asset::AssetKey{
            .content_hash = {
                detail::mix64(name_hash.lo, color_hash),
                detail::mix64(name_hash.hi, mode_params),
            },
            .schema_hash = detail::hash_u64(kAmbientLightingSchema.value),
            .compiler_hash = detail::hash_u64(kAmbientLightingCompilerVersion),
            .deps_hash = deps,
        };
    }

    [[nodiscard]] inline wz::asset::AssetKey make_hdri_environment_key(
        std::string_view name,
        const HDRIEnvironmentCompileDesc& desc) noexcept
    {
        const wz::asset::Hash name_hash = detail::hash_str(name);
        const uint64_t usage_params = detail::mix64(
            detail::mix64(
                static_cast<uint64_t>(desc.format),
                detail::float_bits(desc.exposure)),
            detail::mix64(
                detail::mix64(
                    detail::float_bits(desc.rotation_x_radians),
                    detail::float_bits(desc.rotation_y_radians)),
                detail::mix64(
                    detail::mix64(
                        detail::float_bits(desc.rotation_z_radians),
                        detail::float_bits(desc.lighting_intensity)),
                    detail::mix64(
                        detail::float_bits(desc.reflection_intensity),
                        detail::mix64(
                            detail::float_bits(desc.background_intensity),
                            desc.lighting_sample_resolution)))));
        const uint64_t dominant_direction = detail::mix_float3(
            desc.dominant_light_direction[0],
            desc.dominant_light_direction[1],
            desc.dominant_light_direction[2]);
        const uint64_t environment_color = detail::mix_float3(
            desc.environment_light_color[0],
            desc.environment_light_color[1],
            desc.environment_light_color[2]);
        const uint64_t dominant_color = detail::mix_float3(
            desc.dominant_light_color[0],
            desc.dominant_light_color[1],
            desc.dominant_light_color[2]);
        const uint64_t dominant_params = detail::mix64(
            detail::mix64(
                environment_color,
                detail::float_bits(desc.environment_light_intensity)),
            detail::mix64(
                dominant_direction,
                detail::mix64(
                    dominant_color,
                    detail::mix64(
                        detail::float_bits(desc.dominant_light_intensity),
                        detail::float_bits(desc.dominant_light_confidence)))));

        wz::asset::Hash deps{};
        if (!(desc.source_file == wz::asset::AssetKey{})) {
            deps = detail::combine_dep_hashes(
                deps,
                detail::key_to_dep_hash(desc.source_file));
        }

        return wz::asset::AssetKey{
            .content_hash = {
                detail::mix64(name_hash.lo, usage_params),
                detail::mix64(name_hash.hi, dominant_params),
            },
            .schema_hash = detail::hash_u64(kHDRIEnvironmentSchema.value),
            .compiler_hash = detail::hash_u64(kHDRIEnvironmentCompilerVersion),
            .deps_hash = deps,
        };
    }

} // namespace wz::engine::assets
