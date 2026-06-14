#pragma once

// engine/assets/key_factories/mesh_cluster_hierarchy.h

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/mesh_cluster_hierarchy_asset_module.h>
#include <engine/assets/schema_ids.h>

namespace wz::engine::assets
{
    [[nodiscard]] inline wz::asset::AssetKey
    make_mesh_cluster_hierarchy_key(
        const wz::asset::AssetKey& source_mesh_key,
        const MeshClusterHierarchyDesc& desc) noexcept
    {
        uint64_t h = kMeshClusterHierarchySchema.value;
        h = detail::mix64(h, static_cast<uint64_t>(desc.method));

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(
                kMeshClusterHierarchySchema.value),
            .compiler_hash = detail::hash_u64(
                kMeshClusterHierarchyCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(source_mesh_key),
        };
    }

    [[nodiscard]] inline wz::asset::AssetKey
    make_mesh_cluster_hierarchy_preview_mesh_key(
        const wz::asset::AssetKey& hierarchy_key,
        const MeshClusterHierarchyPreviewMeshDesc& desc) noexcept
    {
        uint64_t h = kMeshClusterHierarchyPreviewMeshSchema.value;
        h = detail::mix64(h, desc.level_index);

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(
                kMeshClusterHierarchyPreviewMeshSchema.value),
            .compiler_hash = detail::hash_u64(
                kMeshClusterHierarchyPreviewMeshCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(hierarchy_key),
        };
    }
}
