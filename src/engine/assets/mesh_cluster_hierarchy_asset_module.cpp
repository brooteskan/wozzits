// src/engine/assets/mesh_cluster_hierarchy_asset_module.cpp

#include <engine/assets/mesh_cluster_hierarchy_asset_module.h>

#include <engine/assets/key_factories/mesh_cluster_hierarchy.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <vector>
#include <utility>

namespace wz::engine::assets
{
    MeshClusterHierarchyAssetModule::MeshClusterHierarchyAssetModule(
        wz::asset::AssetSystem& system,
        MeshClusterHierarchyTable& table)
        : system_(system)
        , table_(table)
    {
    }

    MeshClusterHierarchyAsset
    MeshClusterHierarchyAssetModule::create_mesh_cluster_hierarchy(
        const MeshClusterHierarchyDesc& desc)
    {
        if (!desc.source_mesh.valid()) {
            return {};
        }

        const wz::asset::AssetKey hierarchy_key =
            make_mesh_cluster_hierarchy_key(
                desc.source_mesh.output,
                desc);

        wz::asset::AssetNode node{};
        node.key = hierarchy_key;
        node.type = kAssetTypeMeshClusterHierarchy;
        node.schema = kMeshClusterHierarchySchema;
        node.stage = wz::asset::AssetStage::Source;
        node.meta = desc;

        std::vector<wz::asset::AssetKey> deps{ desc.source_mesh.output };
        if (desc.region_mask && desc.region_mask->field.valid()) {
            deps.push_back(desc.region_mask->field.output);
        }
        if (desc.graph_operator.valid()) {
            deps.push_back(desc.graph_operator.output);
        }
        if (desc.graph_cells_pipeline.valid()) {
            deps.push_back(desc.graph_cells_pipeline.key);
        }
        if (desc.graph_cells_compact_pipeline.valid()) {
            deps.push_back(desc.graph_cells_compact_pipeline.key);
        }

        (void)system_.register_asset(
            std::move(node),
            deps);

        return MeshClusterHierarchyAsset{ .output = hierarchy_key };
    }

    MeshAsset
    MeshClusterHierarchyAssetModule::create_mesh_cluster_hierarchy_preview_mesh(
        const MeshClusterHierarchyPreviewMeshDesc& desc)
    {
        if (!desc.hierarchy.valid()) {
            return {};
        }

        const wz::asset::AssetKey mesh_key =
            make_mesh_cluster_hierarchy_preview_mesh_key(
                desc.hierarchy.output,
                desc);

        wz::asset::AssetNode node{};
        node.key = mesh_key;
        node.type = kAssetTypeMesh;
        node.schema = kMeshClusterHierarchyPreviewMeshSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.meta = desc;

        (void)system_.register_asset(
            std::move(node),
            { desc.hierarchy.output });

        return MeshAsset{ .output = mesh_key };
    }

    MeshClusterHierarchyHandle
    MeshClusterHierarchyAssetModule::get_mesh_cluster_hierarchy(
        const MeshClusterHierarchyAsset& asset) const
    {
        if (!asset.valid()) {
            return {};
        }

        MeshClusterHierarchyHandle out{};
        if (const auto* compiled = system_.find_compiled(asset.output)) {
            out.handle = compiled->handle;
        }
        return out;
    }

    const MeshClusterHierarchyData*
    MeshClusterHierarchyAssetModule::get_mesh_cluster_hierarchy_data(
        MeshClusterHierarchyHandle handle) const
    {
        if (!handle.valid()) {
            return nullptr;
        }
        return table_.get(handle.handle);
    }
}
