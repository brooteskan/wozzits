// src/engine/assets/mesh_sparse_operator_asset_module.cpp

#include <engine/assets/mesh_sparse_operator_asset_module.h>

#include <engine/assets/key_factories/mesh_sparse_operator.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

namespace wz::engine::assets
{
    MeshSparseOperatorAssetModule::MeshSparseOperatorAssetModule(
        wz::asset::AssetSystem& system,
        MeshSparseOperatorTable& table)
        : system_(system)
        , table_(table)
    {
    }

    MeshSparseOperatorAsset
    MeshSparseOperatorAssetModule::create_sparse_operator(
        const MeshSparseOperatorDesc& desc)
    {
        // v0 compiles vertex-domain operators only; the domain field exists
        // in the format so face/edge/cluster operators are additive later.
        if (!desc.source_mesh.valid()
            || desc.domain != MeshOperatorDomain::Vertex)
        {
            return {};
        }

        const wz::asset::AssetKey operator_key =
            make_mesh_sparse_operator_key(desc.source_mesh.output, desc);

        wz::asset::AssetNode node{};
        node.key = operator_key;
        node.type = kAssetTypeMeshSparseOperator;
        node.schema = kMeshSparseOperatorSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.meta = desc;

        // register_asset() returns false when the key is already registered;
        // re-materializing the same scene is expected to hit that, so treat
        // it as success and hand back the existing key.
        (void)system_.register_asset(
            std::move(node),
            { desc.source_mesh.output });

        return MeshSparseOperatorAsset{ .output = operator_key };
    }

    MeshSparseOperatorHandle
    MeshSparseOperatorAssetModule::get_sparse_operator(
        const MeshSparseOperatorAsset& asset) const
    {
        if (!asset.valid()) {
            return {};
        }

        MeshSparseOperatorHandle out{};
        if (const auto* compiled = system_.find_compiled(asset.output)) {
            out.handle = compiled->handle;
        }
        return out;
    }

    const MeshSparseOperatorData*
    MeshSparseOperatorAssetModule::get_sparse_operator_data(
        MeshSparseOperatorHandle handle) const
    {
        if (!handle.valid()) {
            return nullptr;
        }
        return table_.get(handle.handle);
    }
}
