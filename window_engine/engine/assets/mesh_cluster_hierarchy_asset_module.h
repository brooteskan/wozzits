#pragma once

// engine/assets/mesh_cluster_hierarchy_asset_module.h

#include <asset/system.h>
#include <engine/assets/compute_pipeline_asset_module.h>
#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/mesh_cluster_hierarchy/mesh_cluster_hierarchy.h>
#include <engine/assets/mesh_derived_field_asset_module.h>
#include <engine/assets/mesh_render_style/mesh_render_style.h>
#include <engine/assets/mesh_sparse_operator_asset_module.h>

#include <optional>
#include <string>

namespace wz::engine::assets
{
    struct MeshClusterHierarchyRegionMaskDesc
    {
        MeshDerivedFieldAsset field{};
        MeshMaskRenderStyleData mask{};
    };

    struct MeshClusterHierarchyDesc
    {
        std::string name;
        MeshAsset source_mesh;
        MeshClusterHierarchyBuildMethod method =
            MeshClusterHierarchyBuildMethod::Identity;
        uint32_t max_level_index = 3;
        MeshSparseOperatorAsset graph_operator{};
        ComputePipelineAsset graph_cells_pipeline{};
        ComputePipelineAsset graph_cells_compact_pipeline{};
        std::optional<MeshClusterHierarchyRegionMaskDesc> region_mask;
    };

    struct MeshClusterHierarchyAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct MeshClusterHierarchyPreviewMeshDesc
    {
        std::string name;
        MeshClusterHierarchyAsset hierarchy;
        uint32_t level_index = 0;
    };

    struct MeshClusterHierarchyHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    class MeshClusterHierarchyAssetModule
    {
    public:
        MeshClusterHierarchyAssetModule(
            wz::asset::AssetSystem& system,
            MeshClusterHierarchyTable& table);

        [[nodiscard]] MeshClusterHierarchyAsset create_mesh_cluster_hierarchy(
            const MeshClusterHierarchyDesc& desc);

        [[nodiscard]] MeshAsset create_mesh_cluster_hierarchy_preview_mesh(
            const MeshClusterHierarchyPreviewMeshDesc& desc);

        [[nodiscard]] MeshClusterHierarchyHandle get_mesh_cluster_hierarchy(
            const MeshClusterHierarchyAsset& asset) const;

        [[nodiscard]] const MeshClusterHierarchyData*
        get_mesh_cluster_hierarchy_data(
            MeshClusterHierarchyHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        MeshClusterHierarchyTable& table_;
    };
}
