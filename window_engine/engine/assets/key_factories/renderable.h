#pragma once

// engine/assets/key_factories/renderable.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/renderable/renderable.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

#include <cstring>
#include <string_view>

namespace wz::engine::assets
{
    [[nodiscard]] inline uint64_t terrain_lighting_float_bits(
        float value) noexcept
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return static_cast<uint64_t>(bits);
    }

    [[nodiscard]] inline uint64_t mix_terrain_lighting_data(
        uint64_t h,
        const TerrainLightingData& lighting) noexcept
    {
        h = detail::mix64(h, static_cast<uint64_t>(lighting.mode));
        for (float channel : lighting.environment_color) {
            h = detail::mix64(h, terrain_lighting_float_bits(channel));
        }
        h = detail::mix64(h,
            terrain_lighting_float_bits(lighting.environment_intensity));
        for (float axis : lighting.dominant_light_direction) {
            h = detail::mix64(h, terrain_lighting_float_bits(axis));
        }
        for (float channel : lighting.dominant_light_color) {
            h = detail::mix64(h, terrain_lighting_float_bits(channel));
        }
        h = detail::mix64(h,
            terrain_lighting_float_bits(lighting.dominant_light_intensity));
        h = detail::mix64(h,
            terrain_lighting_float_bits(lighting.sky_visibility_strength));
        h = detail::mix64(h,
            terrain_lighting_float_bits(lighting.normal_lighting_strength));
        h = detail::mix64(h,
            terrain_lighting_float_bits(lighting.terrain_bounce_strength));
        return h;
    }

    [[nodiscard]] inline wz::asset::AssetKey make_mesh_wireframe_renderable_key(
        std::string_view name,
        const wz::asset::AssetKey& mesh_key,
        BuiltinRenderProgram program = BuiltinRenderProgram::MeshWireframeDebug,
        uint32_t policy_flags = RenderPolicy_Wireframe,
        RenderDomain domain = RenderDomain::Debug) noexcept
    {
        uint64_t h = detail::fnv1a_64(name);
        h = detail::mix64(h, static_cast<uint64_t>(program));
        h = detail::mix64(h, static_cast<uint64_t>(policy_flags));
        h = detail::mix64(h, static_cast<uint64_t>(domain));

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(kMeshWireframeRenderableSchema.value),
            .compiler_hash = detail::hash_u64(kMeshWireframeRenderableCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(mesh_key),
        };
    }

    [[nodiscard]] inline wz::asset::AssetKey make_gaussian_splat_debug_renderable_key(
        std::string_view name,
        const wz::asset::AssetKey& splat_cloud_key,
        const wz::asset::AssetKey& color_lod_key = {}) noexcept
    {
        const uint64_t h = detail::fnv1a_64(name);

        // deps_hash combines the source cloud and optional color-LOD keys
        // so renderables with different LOD attachments produce distinct
        // identities.  An empty color_lod_key collapses to zero and matches
        // the pre-LOD behaviour bit-for-bit.
        const wz::asset::Hash cloud_dep = detail::key_to_dep_hash(splat_cloud_key);
        const wz::asset::Hash lod_dep =
            (color_lod_key == wz::asset::AssetKey{})
                ? wz::asset::Hash{}
                : detail::key_to_dep_hash(color_lod_key);

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(kGaussianSplatDebugRenderableSchema.value),
            .compiler_hash = detail::hash_u64(kGaussianSplatDebugRenderableCompilerVersion),
            .deps_hash = wz::asset::Hash{
                detail::mix64(cloud_dep.lo, lod_dep.lo),
                detail::mix64(cloud_dep.hi, lod_dep.hi),
            },
        };
    }

    [[nodiscard]] inline wz::asset::AssetKey make_scalar_field_debug_renderable_key(
        std::string_view name,
        const wz::asset::AssetKey& scalar_field_key) noexcept
    {
        const uint64_t h = detail::fnv1a_64(name);

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(kScalarFieldDebugRenderableSchema.value),
            .compiler_hash = detail::hash_u64(kScalarFieldDebugRenderableCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(scalar_field_key),
        };
    }

    [[nodiscard]] inline wz::asset::AssetKey make_mesh_styled_renderable_key(
        std::string_view name,
        const wz::asset::AssetKey& mesh_key,
        const wz::asset::AssetKey& style_key,
        const wz::asset::AssetKey& mesh_field_visualization_key = {},
        const wz::asset::AssetKey& render_program_key = {}) noexcept
    {
        const uint64_t h = detail::fnv1a_64(name);
        const wz::asset::Hash mesh_dep = detail::key_to_dep_hash(mesh_key);
        const wz::asset::Hash style_dep = detail::key_to_dep_hash(style_key);
        const wz::asset::Hash field_dep =
            (mesh_field_visualization_key == wz::asset::AssetKey{})
                ? wz::asset::Hash{}
                : detail::key_to_dep_hash(mesh_field_visualization_key);
        const wz::asset::Hash program_dep =
            (render_program_key == wz::asset::AssetKey{})
                ? wz::asset::Hash{}
                : detail::key_to_dep_hash(render_program_key);

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(kMeshStyledRenderableSchema.value),
            .compiler_hash = detail::hash_u64(kMeshStyledRenderableCompilerVersion),
            .deps_hash = wz::asset::Hash{
                detail::mix64(
                    detail::mix64(
                        detail::mix64(mesh_dep.lo, style_dep.lo),
                        field_dep.lo),
                    program_dep.lo),
                detail::mix64(
                    detail::mix64(
                        detail::mix64(mesh_dep.hi, style_dep.hi),
                        field_dep.hi),
                    program_dep.hi),
            },
        };
    }

    [[nodiscard]] inline wz::asset::AssetKey make_rhi_pull_mesh_renderable_key(
        std::string_view name,
        const wz::asset::AssetKey& mesh_key,
        const wz::asset::AssetKey& render_program_key) noexcept
    {
        const uint64_t h = detail::fnv1a_64(name);
        const wz::asset::Hash mesh_dep = detail::key_to_dep_hash(mesh_key);
        const wz::asset::Hash program_dep =
            detail::key_to_dep_hash(render_program_key);

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash =
                detail::hash_u64(kRhiPullMeshRenderableSchema.value),
            .compiler_hash =
                detail::hash_u64(kRhiPullMeshRenderableCompilerVersion),
            .deps_hash = wz::asset::Hash{
                detail::mix64(mesh_dep.lo, program_dep.lo),
                detail::mix64(mesh_dep.hi, program_dep.hi),
            },
        };
    }

    [[nodiscard]] inline wz::asset::AssetKey
    make_gpu_sparse_mesh_renderable_key(
        std::string_view name,
        const wz::asset::AssetKey& sparse_mesh_key,
        const wz::asset::AssetKey& render_program_key) noexcept
    {
        const uint64_t h = detail::fnv1a_64(name);
        const wz::asset::Hash mesh_dep =
            detail::key_to_dep_hash(sparse_mesh_key);
        const wz::asset::Hash program_dep =
            detail::key_to_dep_hash(render_program_key);

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash =
                detail::hash_u64(kGpuSparseMeshRenderableSchema.value),
            .compiler_hash =
                detail::hash_u64(kGpuSparseMeshRenderableCompilerVersion),
            .deps_hash = wz::asset::Hash{
                detail::mix64(mesh_dep.lo, program_dep.lo),
                detail::mix64(mesh_dep.hi, program_dep.hi),
            },
        };
    }

    [[nodiscard]] inline uint64_t clipmap_landscape_settings_float_bits(
        float value) noexcept
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return static_cast<uint64_t>(bits);
    }

    [[nodiscard]] inline uint64_t mix_clipmap_landscape_settings(
        uint64_t h,
        const ClipmapLandscapeRenderSettings& settings) noexcept
    {
        for (float axis : settings.world_origin) {
            h = detail::mix64(h, clipmap_landscape_settings_float_bits(axis));
        }
        for (float axis : settings.world_size) {
            h = detail::mix64(h, clipmap_landscape_settings_float_bits(axis));
        }
        h = detail::mix64(
            h, clipmap_landscape_settings_float_bits(settings.vertical_scale));
        h = detail::mix64(
            h, clipmap_landscape_settings_float_bits(settings.base_height));
        h = detail::mix64(
            h,
            clipmap_landscape_settings_float_bits(
                settings.lattice_world_cell_size));
        return h;
    }

    [[nodiscard]] inline wz::asset::AssetKey
    make_clipmap_landscape_renderable_key(
        std::string_view name,
        const wz::asset::AssetKey& lattice_mesh_key,
        const wz::asset::AssetKey& height_field_key,
        const wz::asset::AssetKey& render_program_key,
        const ClipmapLandscapeRenderSettings& settings = {}) noexcept
    {
        uint64_t h = detail::fnv1a_64(name);
        h = mix_clipmap_landscape_settings(h, settings);

        const wz::asset::Hash mesh_dep =
            detail::key_to_dep_hash(lattice_mesh_key);
        const wz::asset::Hash height_dep =
            detail::key_to_dep_hash(height_field_key);
        const wz::asset::Hash program_dep =
            detail::key_to_dep_hash(render_program_key);

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash =
                detail::hash_u64(kClipmapLandscapeRenderableSchema.value),
            .compiler_hash =
                detail::hash_u64(kClipmapLandscapeRenderableCompilerVersion),
            .deps_hash = wz::asset::Hash{
                detail::mix64(
                    detail::mix64(mesh_dep.lo, height_dep.lo),
                    program_dep.lo),
                detail::mix64(
                    detail::mix64(mesh_dep.hi, height_dep.hi),
                    program_dep.hi),
            },
        };
    }

    [[nodiscard]] inline wz::asset::AssetKey make_terrain_debug_renderable_key(
        std::string_view name,
        const wz::asset::AssetKey& terrain_key,
        BuiltinRenderProgram mesh_program =
            BuiltinRenderProgram::MeshWireframeDepthDebug,
        uint32_t mesh_policy_flags =
            RenderPolicy_Wireframe
            | RenderPolicy_DepthTest
            | RenderPolicy_DepthWrite,
        RenderDomain domain = RenderDomain::Debug) noexcept
    {
        uint64_t h = detail::fnv1a_64(name);
        h = detail::mix64(h, static_cast<uint64_t>(mesh_program));
        h = detail::mix64(h, static_cast<uint64_t>(mesh_policy_flags));
        h = detail::mix64(h, static_cast<uint64_t>(domain));

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(kTerrainDebugRenderableSchema.value),
            .compiler_hash = detail::hash_u64(kTerrainDebugRenderableCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(terrain_key),
        };
    }

    [[nodiscard]] inline wz::asset::AssetKey make_terrain_surface_renderable_key(
        std::string_view name,
        const wz::asset::AssetKey& terrain_key,
        const wz::asset::AssetKey& visual_proxy_key,
        BuiltinRenderProgram mesh_program =
            BuiltinRenderProgram::TerrainMeshSurface,
        uint32_t mesh_policy_flags =
            RenderPolicy_DepthTest
            | RenderPolicy_DepthWrite,
        RenderDomain domain = RenderDomain::Opaque,
        const TerrainLightingData& lighting = {},
        float target_pixels_per_triangle = 0.0f) noexcept
    {
        uint64_t h = detail::fnv1a_64(name);
        h = detail::mix64(h, static_cast<uint64_t>(mesh_program));
        h = detail::mix64(h, static_cast<uint64_t>(mesh_policy_flags));
        h = detail::mix64(h, static_cast<uint64_t>(domain));
        h = mix_terrain_lighting_data(h, lighting);
        h = detail::mix64(
            h,
            terrain_lighting_float_bits(target_pixels_per_triangle));

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(kTerrainSurfaceRenderableSchema.value),
            .compiler_hash = detail::hash_u64(kTerrainSurfaceRenderableCompilerVersion),
            .deps_hash = wz::asset::Hash{
                detail::mix64(
                    detail::key_to_dep_hash(terrain_key).lo,
                    detail::key_to_dep_hash(visual_proxy_key).lo),
                detail::mix64(
                    detail::key_to_dep_hash(terrain_key).hi,
                    detail::key_to_dep_hash(visual_proxy_key).hi),
            },
        };
    }
}
