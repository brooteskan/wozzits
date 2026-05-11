#pragma once
// window_engine/engine/assets/key_factories/mesh.h

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>

namespace wz::engine::assets
{
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
}