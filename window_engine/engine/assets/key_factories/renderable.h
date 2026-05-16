#pragma once

// engine/assets/key_factories/renderable.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

#include <string_view>

namespace wz::engine::assets
{
    [[nodiscard]] inline wz::asset::AssetKey make_mesh_wireframe_renderable_key(
        std::string_view name,
        const wz::asset::AssetKey& mesh_key) noexcept
    {
        const uint64_t h = detail::fnv1a_64(name);

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(kMeshWireframeRenderableSchema.value),
            .compiler_hash = detail::hash_u64(kMeshWireframeRenderableCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(mesh_key),
        };
    }

    [[nodiscard]] inline wz::asset::AssetKey make_gaussian_splat_debug_renderable_key(
        std::string_view name,
        const wz::asset::AssetKey& splat_cloud_key) noexcept
    {
        const uint64_t h = detail::fnv1a_64(name);

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(kGaussianSplatDebugRenderableSchema.value),
            .compiler_hash = detail::hash_u64(kGaussianSplatDebugRenderableCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(splat_cloud_key),
        };
    }
}