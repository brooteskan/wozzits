// src/engine/assets/gpu_sparse_mesh_asset_module.cpp

#include <engine/assets/gpu_sparse_mesh_asset_module.h>

#include <engine/assets/key_factories/gpu_sparse_mesh.h>
#include <engine/assets/key_factories/mesh_sparse_operator.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

namespace wz::engine::assets
{
    GpuSparseMeshAssetModule::GpuSparseMeshAssetModule(
        wz::asset::AssetSystem& system,
        GpuSparseMeshTable& table)
        : system_(system)
        , table_(table)
    {
    }

    GpuSparseMeshAsset GpuSparseMeshAssetModule::create_gpu_sparse_mesh(
        const GpuSparseMeshDesc& desc)
    {
        if (!desc.source_mesh.valid()
            || desc.operator_domain != MeshOperatorDomain::Vertex)
        {
            return {};
        }

        const wz::asset::AssetKey sparse_mesh_key =
            make_gpu_sparse_mesh_key(desc.source_mesh.output, desc);

        const MeshSparseOperatorDesc operator_desc{
            .source_mesh = desc.source_mesh,
            .kind = desc.operator_kind,
            .domain = desc.operator_domain,
        };
        const wz::asset::AssetKey operator_key =
            make_mesh_sparse_operator_key(
                desc.source_mesh.output,
                operator_desc);

        wz::asset::AssetNode operator_node{};
        operator_node.key = operator_key;
        operator_node.type = kAssetTypeMeshSparseOperator;
        operator_node.schema = kMeshSparseOperatorSchema;
        operator_node.stage = wz::asset::AssetStage::Source;
        operator_node.meta = operator_desc;

        (void)system_.register_asset(
            std::move(operator_node),
            { desc.source_mesh.output });

        wz::asset::AssetNode node{};
        node.key = sparse_mesh_key;
        node.type = kAssetTypeGpuSparseMesh;
        node.schema = kGpuSparseMeshFromMeshSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.meta = desc;

        (void)system_.register_asset(
            std::move(node),
            { desc.source_mesh.output, operator_key });

        return GpuSparseMeshAsset{ .output = sparse_mesh_key };
    }

    GpuSparseMeshHandle GpuSparseMeshAssetModule::get_gpu_sparse_mesh(
        const GpuSparseMeshAsset& asset) const
    {
        if (!asset.valid()) {
            return {};
        }

        GpuSparseMeshHandle out{};
        if (const auto* compiled = system_.find_compiled(asset.output)) {
            out.handle = compiled->handle;
        }
        return out;
    }

    const GpuSparseMeshData*
    GpuSparseMeshAssetModule::get_gpu_sparse_mesh_data(
        GpuSparseMeshHandle handle) const
    {
        if (!handle.valid()) {
            return nullptr;
        }
        return table_.get(handle.handle);
    }
}
