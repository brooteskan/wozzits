// src/engine/assets/mesh_cluster_hierarchy/mesh_cluster_hierarchy_compilers.cpp

#include <engine/assets/mesh_cluster_hierarchy/mesh_cluster_hierarchy_compilers.h>

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/mesh_derived_field/mesh_derived_field.h>
#include <engine/assets/mesh_cluster_hierarchy_asset_module.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <any>
#include <utility>

namespace wz::engine::assets::internal
{
    namespace
    {
        wz::asset::AssetNode compiled_node(
            const wz::asset::AssetNode& input,
            wz::asset::ResourceHandle handle)
        {
            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = handle;
            return out;
        }

        MeshClusterHierarchyData build_identity_hierarchy(
            const MeshClusterHierarchyDesc& desc,
            const MeshData& source_mesh)
        {
            MeshClusterHierarchyData data{};
            data.source_mesh_key = desc.source_mesh.output;
            data.source_topology_hash =
                compute_mesh_topology_hash(source_mesh);
            data.method = desc.method;

            MeshClusterHierarchyLevel level{};
            level.level_index = 0;
            level.cluster_count = source_mesh.index_count() > 0u ? 1u : 0u;
            level.vertex_count = source_mesh.vertex_count();
            level.triangle_count = source_mesh.index_count() / 3u;
            level.conservative_error = 0.0f;
            level.preview_mesh = source_mesh;

            data.levels.push_back(std::move(level));
            return data;
        }

        wz::asset::AssetNode compile_mesh_cluster_hierarchy_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::ResourceHandle> dep_handles,
            wz::Logger& logger,
            MeshTable& mesh_table,
            MeshClusterHierarchyTable& hierarchy_table)
        {
            const auto* desc =
                std::any_cast<MeshClusterHierarchyDesc>(&input.meta);
            if (!desc || dep_handles.size() != 1u) {
                logger.error("mesh cluster hierarchy node missing desc");
                return compile_failed_node(input);
            }

            const MeshData* source_mesh = mesh_table.get(dep_handles[0]);
            if (!source_mesh || !source_mesh->valid()) {
                logger.error("mesh cluster hierarchy source mesh is invalid");
                return compile_failed_node(input);
            }

            MeshClusterHierarchyData data{};
            switch (desc->method) {
            case MeshClusterHierarchyBuildMethod::Identity:
                data = build_identity_hierarchy(*desc, *source_mesh);
                break;

            case MeshClusterHierarchyBuildMethod::GraphCoarsen:
                logger.error(
                    "mesh cluster hierarchy graph coarsen is not implemented");
                return compile_failed_node(input);
            }

            if (!data.valid()) {
                logger.error(
                    "mesh cluster hierarchy compiler produced invalid data");
                return compile_failed_node(input);
            }

            const wz::asset::ResourceHandle handle =
                hierarchy_table.add(std::move(data));
            return handle.valid()
                ? compiled_node(input, handle)
                : compile_failed_node(input);
        }

        wz::asset::AssetNode compile_mesh_cluster_hierarchy_preview_mesh_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::ResourceHandle> dep_handles,
            wz::Logger& logger,
            MeshTable& mesh_table,
            MeshClusterHierarchyTable& hierarchy_table)
        {
            const auto* desc =
                std::any_cast<MeshClusterHierarchyPreviewMeshDesc>(
                    &input.meta);
            if (!desc || dep_handles.size() != 1u) {
                logger.error(
                    "mesh cluster hierarchy preview mesh node missing desc");
                return compile_failed_node(input);
            }

            const MeshClusterHierarchyData* hierarchy =
                hierarchy_table.get(dep_handles[0]);
            if (!hierarchy || !hierarchy->valid()) {
                logger.error(
                    "mesh cluster hierarchy preview source is invalid");
                return compile_failed_node(input);
            }

            if (desc->level_index >= hierarchy->level_count()) {
                logger.error(
                    "mesh cluster hierarchy preview level is out of range");
                return compile_failed_node(input);
            }

            const MeshData& preview =
                hierarchy->levels[desc->level_index].preview_mesh;
            if (!preview.valid()) {
                logger.error(
                    "mesh cluster hierarchy preview level is invalid");
                return compile_failed_node(input);
            }

            const wz::asset::ResourceHandle handle =
                mesh_table.add(preview);
            return handle.valid()
                ? compiled_node(input, handle)
                : compile_failed_node(input);
        }
    }

    void register_mesh_cluster_hierarchy_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshTable& mesh_table,
        MeshClusterHierarchyTable& hierarchy_table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kMeshClusterHierarchySchema,
            .output_type = kAssetTypeMeshClusterHierarchy,
            .compile = [
                &logger,
                &mesh_table,
                &hierarchy_table](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode>,
                    std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                return compile_mesh_cluster_hierarchy_node(
                    input,
                    dep_handles,
                    logger,
                    mesh_table,
                    hierarchy_table);
            }
        });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kMeshClusterHierarchyPreviewMeshSchema,
            .output_type = kAssetTypeMesh,
            .compile = [
                &logger,
                &mesh_table,
                &hierarchy_table](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode>,
                    std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                return compile_mesh_cluster_hierarchy_preview_mesh_node(
                    input,
                    dep_handles,
                    logger,
                    mesh_table,
                    hierarchy_table);
            }
        });
    }
}
