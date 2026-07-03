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

    // The optional style_key (issue #195 slice A) folds into deps_hash in graph
    // registration order (mesh, program, style). An empty style_key collapses to
    // zero and reproduces the pre-slice-A 2-dep key bit-for-bit, so unstyled pull
    // meshes keep their identity.
    [[nodiscard]] inline wz::asset::AssetKey make_rhi_pull_mesh_renderable_key(
        std::string_view name,
        const wz::asset::AssetKey& mesh_key,
        const wz::asset::AssetKey& render_program_key,
        const wz::asset::AssetKey& style_key = {}) noexcept
    {
        const uint64_t h = detail::fnv1a_64(name);
        const wz::asset::Hash mesh_dep = detail::key_to_dep_hash(mesh_key);
        const wz::asset::Hash program_dep =
            detail::key_to_dep_hash(render_program_key);

        wz::asset::Hash deps_hash = wz::asset::Hash{
            detail::mix64(mesh_dep.lo, program_dep.lo),
            detail::mix64(mesh_dep.hi, program_dep.hi),
        };
        if (!(style_key == wz::asset::AssetKey{})) {
            deps_hash = detail::combine_dep_hashes(
                deps_hash, detail::key_to_dep_hash(style_key));
        }

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash =
                detail::hash_u64(kRhiPullMeshRenderableSchema.value),
            .compiler_hash =
                detail::hash_u64(kRhiPullMeshRenderableCompilerVersion),
            .deps_hash = deps_hash,
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

    [[nodiscard]] inline wz::asset::AssetKey
    make_gaussian_splat_cloud_rhi_renderable_key(
        std::string_view name,
        const wz::asset::AssetKey& splat_cloud_key,
        const wz::asset::AssetKey& render_program_key,
        const GaussianSplatCloudRenderSettings& settings = {}) noexcept
    {
        uint64_t h = detail::fnv1a_64(name);
        uint32_t size_bits = 0;
        std::memcpy(&size_bits, &settings.splat_size, sizeof(size_bits));
        h = detail::mix64(h, static_cast<uint64_t>(size_bits));

        const wz::asset::Hash cloud_dep =
            detail::key_to_dep_hash(splat_cloud_key);
        const wz::asset::Hash program_dep =
            detail::key_to_dep_hash(render_program_key);

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash =
                detail::hash_u64(kGaussianSplatCloudRhiRenderableSchema.value),
            .compiler_hash = detail::hash_u64(
                kGaussianSplatCloudRhiRenderableCompilerVersion),
            .deps_hash = wz::asset::Hash{
                detail::mix64(cloud_dep.lo, program_dep.lo),
                detail::mix64(cloud_dep.hi, program_dep.hi),
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
        // placement_authoritative is part of identity (issue #218 Phase 2): a
        // placement-driven footprint must produce a distinct recipe from an
        // identically-valued authored one.
        h = detail::mix64(
            h, settings.placement_authoritative ? 1ull : 0ull);
        return h;
    }

    // deps_hash folds lattice, height, program — then, if present, the OPTIONAL
    // placement dep (issue #218 Phase 2), in the SAME order the module registers
    // the graph edges (lattice, height, program, placement). combine_dep_hashes
    // is not commutative, so this ordering must match registration order.
    // Folding the placement here is what makes a placement change re-compile the
    // renderable. A null/default placement key reproduces the original 3-dep
    // key exactly, so back-compat is preserved.
    [[nodiscard]] inline wz::asset::AssetKey
    make_clipmap_landscape_renderable_key(
        std::string_view name,
        const wz::asset::AssetKey& lattice_mesh_key,
        const wz::asset::AssetKey& height_field_key,
        const wz::asset::AssetKey& render_program_key,
        const ClipmapLandscapeRenderSettings& settings = {},
        const wz::asset::AssetKey& placement_key = {}) noexcept
    {
        uint64_t h = detail::fnv1a_64(name);
        h = mix_clipmap_landscape_settings(h, settings);

        const wz::asset::Hash mesh_dep =
            detail::key_to_dep_hash(lattice_mesh_key);
        const wz::asset::Hash height_dep =
            detail::key_to_dep_hash(height_field_key);
        const wz::asset::Hash program_dep =
            detail::key_to_dep_hash(render_program_key);

        wz::asset::Hash deps_hash = wz::asset::Hash{
            detail::mix64(
                detail::mix64(mesh_dep.lo, height_dep.lo),
                program_dep.lo),
            detail::mix64(
                detail::mix64(mesh_dep.hi, height_dep.hi),
                program_dep.hi),
        };
        if (!(placement_key == wz::asset::AssetKey{})) {
            deps_hash = detail::combine_dep_hashes(
                deps_hash,
                detail::key_to_dep_hash(placement_key));
        }

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash =
                detail::hash_u64(kClipmapLandscapeRenderableSchema.value),
            .compiler_hash =
                detail::hash_u64(kClipmapLandscapeRenderableCompilerVersion),
            .deps_hash = deps_hash,
        };
    }

    // Custom renderable key (issue #229). Content folds the name + every
    // authored binding ROW (semantic + its source key — the key must live in
    // content as well as deps so a half-authored row, semantic with no wired
    // source, keys differently from a wired one) + every authored constant
    // (name + value bits). deps_hash folds mesh, program, then each WIRED
    // binding source in authored order — the SAME order the module registers
    // the graph edges (combine_dep_hashes is not commutative). Per-instance
    // constant OVERRIDES (scene-node data) deliberately have no representation
    // here: they merge at pack time, so editing one never re-keys the asset.
    [[nodiscard]] inline wz::asset::AssetKey make_custom_renderable_key(
        std::string_view name,
        const CustomRenderableCompileDesc& desc) noexcept
    {
        uint64_t h = detail::fnv1a_64(name);
        for (const CustomRenderableCompileDesc::Binding& binding :
             desc.bindings)
        {
            h = detail::mix64(h, detail::fnv1a_64(binding.semantic));
            const wz::asset::Hash source = detail::key_to_dep_hash(binding.asset);
            h = detail::mix64(h, source.lo);
            h = detail::mix64(h, source.hi);
        }
        for (const CustomRenderableCompileDesc::Constant& constant :
             desc.constants)
        {
            h = detail::mix64(h, detail::fnv1a_64(constant.name));
            for (float component : constant.value) {
                h = detail::mix64(
                    h, terrain_lighting_float_bits(component));
            }
        }

        const wz::asset::Hash mesh_dep =
            detail::key_to_dep_hash(desc.mesh_asset);
        const wz::asset::Hash program_dep =
            detail::key_to_dep_hash(desc.render_program_asset);
        wz::asset::Hash deps_hash = wz::asset::Hash{
            detail::mix64(mesh_dep.lo, program_dep.lo),
            detail::mix64(mesh_dep.hi, program_dep.hi),
        };
        for (const CustomRenderableCompileDesc::Binding& binding :
             desc.bindings)
        {
            if (binding.asset == wz::asset::AssetKey{}) {
                continue;  // unwired row: no graph edge, no dep fold
            }
            deps_hash = detail::combine_dep_hashes(
                deps_hash, detail::key_to_dep_hash(binding.asset));
        }

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash =
                detail::hash_u64(kCustomRenderableSchema.value),
            .compiler_hash =
                detail::hash_u64(kCustomRenderableCompilerVersion),
            .deps_hash = deps_hash,
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
