#pragma once

// engine/assets/key_factories/mesh_cluster_hierarchy.h

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/mesh_cluster_hierarchy_asset_module.h>
#include <engine/assets/key_factories/mesh_render_style.h>
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
        h = detail::mix64(h, desc.region_mask.has_value() ? 1ull : 0ull);
        if (desc.region_mask) {
            const MeshMaskRenderStyleData& mask = desc.region_mask->mask;
            h = detail::mix64(h, mask.enabled ? 1ull : 0ull);
            h = detail::mix64(h, static_cast<uint64_t>(mask.domain));
            h = detail::mix64(
                h,
                static_cast<uint64_t>(mask.projection_mode));
            h = detail::mix64(
                h,
                static_cast<uint64_t>(mask.overlap_mode));
            for (float channel : mask.unmatched_color) {
                h = detail::mix64(h, mesh_render_style_float_bits(channel));
            }
            h = detail::mix64(h, mask.show_unmatched ? 1ull : 0ull);
            h = detail::mix64(
                h,
                static_cast<uint64_t>(mask.rules.size()));
            for (const MeshMaskRule& rule : mask.rules) {
                h = detail::mix64(h, rule.enabled ? 1ull : 0ull);
                h = detail::mix64(h, rule.input_channel_id);
                h = detail::mix64(h, mesh_render_style_float_bits(rule.lo));
                h = detail::mix64(h, mesh_render_style_float_bits(rule.hi));
                for (float channel : rule.color) {
                    h = detail::mix64(
                        h,
                        mesh_render_style_float_bits(channel));
                }
                h = detail::mix64(
                    h,
                    static_cast<uint64_t>(
                        static_cast<int64_t>(rule.priority)));
            }
        }

        wz::asset::Hash deps_hash =
            detail::key_to_dep_hash(source_mesh_key);
        if (desc.region_mask && desc.region_mask->field.valid()) {
            deps_hash = detail::combine_dep_hashes(
                deps_hash,
                detail::key_to_dep_hash(desc.region_mask->field.output));
        }

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(
                kMeshClusterHierarchySchema.value),
            .compiler_hash = detail::hash_u64(
                kMeshClusterHierarchyCompilerVersion),
            .deps_hash = deps_hash,
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
