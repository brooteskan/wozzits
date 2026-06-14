#pragma once
// window_engine/engine/assets/key_factories/mesh.h

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/schema_ids.h>

#include <cstring>

namespace wz::engine::assets
{
    [[nodiscard]] inline uint64_t mesh_key_float_bits(float value) noexcept
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return static_cast<uint64_t>(bits);
    }

    [[nodiscard]] inline wz::asset::AssetKey make_procedural_triangle_mesh_key() noexcept
    {
        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(kProceduralTriangleMeshSchema.value),
            .schema_hash = detail::hash_u64(kProceduralTriangleMeshSchema.value),
            .compiler_hash = detail::hash_u64(kMeshCompilerVersion),
            .deps_hash = {},
        };
    }

    [[nodiscard]] inline wz::asset::AssetKey make_procedural_quad_mesh_key() noexcept
    {
        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(kProceduralQuadMeshSchema.value),
            .schema_hash = detail::hash_u64(kProceduralQuadMeshSchema.value),
            .compiler_hash = detail::hash_u64(kMeshCompilerVersion),
            .deps_hash = {},
        };
    }

    [[nodiscard]] inline wz::asset::AssetKey make_procedural_cube_mesh_key() noexcept
    {
        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(kProceduralCubeMeshSchema.value),
            .schema_hash = detail::hash_u64(kProceduralCubeMeshSchema.value),
            .compiler_hash = detail::hash_u64(kMeshCompilerVersion),
            .deps_hash = {},
        };
    }

    [[nodiscard]] inline wz::asset::AssetKey make_placeholder_mesh_key() noexcept
    {
        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(kPlaceholderMeshSchema.value),
            .schema_hash = detail::hash_u64(kPlaceholderMeshSchema.value),
            .compiler_hash = detail::hash_u64(kMeshCompilerVersion),
            .deps_hash = {},
        };
    }

    [[nodiscard]] inline wz::asset::AssetKey make_glb_mesh_key(
        const wz::asset::AssetKey& source_file_key,
        uint32_t mesh_index = 0) noexcept
    {
        const uint64_t content = detail::mix64(
            kGLBMeshSchema.value,
            static_cast<uint64_t>(mesh_index));

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(content),
            .schema_hash = detail::hash_u64(kGLBMeshSchema.value),
            .compiler_hash = detail::hash_u64(kMeshCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(source_file_key),
        };
    }

    [[nodiscard]] inline wz::asset::AssetKey make_decimated_mesh_key(
        const wz::asset::AssetKey& source_mesh_key,
        const MeshDecimationAssetDesc& desc) noexcept
    {
        uint64_t h = kMeshDecimationSchema.value;
        h = detail::mix64(h, static_cast<uint64_t>(desc.target_vertex_count));
        h = detail::mix64(h, static_cast<uint64_t>(desc.target_triangle_count));
        h = detail::mix64(h, mesh_key_float_bits(desc.target_ratio));
        h = detail::mix64(h, desc.preserve_boundary ? 1ull : 0ull);
        h = detail::mix64(h, mesh_key_float_bits(desc.aspect_ratio));
        h = detail::mix64(h, mesh_key_float_bits(desc.edge_length));
        h = detail::mix64(h, static_cast<uint64_t>(desc.max_valence));
        h = detail::mix64(h, mesh_key_float_bits(desc.normal_deviation));
        h = detail::mix64(h, mesh_key_float_bits(desc.hausdorff_error));

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(kMeshDecimationSchema.value),
            .compiler_hash = detail::hash_u64(kMeshCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(source_mesh_key),
        };
    }

    [[nodiscard]] inline wz::asset::AssetKey
    make_debug_triangle_stride_mesh_key(
        const wz::asset::AssetKey& source_mesh_key,
        const DebugTriangleStrideMeshDesc& desc) noexcept
    {
        uint64_t h = kDebugTriangleStrideMeshSchema.value;
        h = detail::mix64(h, static_cast<uint64_t>(desc.stride));
        h = detail::mix64(h, static_cast<uint64_t>(desc.phase));

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(
                kDebugTriangleStrideMeshSchema.value),
            .compiler_hash = detail::hash_u64(kMeshCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(source_mesh_key),
        };
    }
}
