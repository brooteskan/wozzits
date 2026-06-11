#pragma once

// engine/assets/mesh_sparse_operator_asset_module.h

#include <asset/system.h>
#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/mesh_sparse_operator/mesh_sparse_operator.h>

#include <string>

namespace wz::engine::assets
{
    // Recipe for a sparse operator over a source mesh. kind selects the
    // operator within the single MeshSparseOperator format; v0 compiles
    // UniformVertexLaplacian on the Vertex domain only.
    struct MeshSparseOperatorDesc
    {
        std::string name;
        MeshAsset source_mesh;
        MeshSparseOperatorKind kind =
            MeshSparseOperatorKind::UniformVertexLaplacian;
        MeshOperatorDomain domain = MeshOperatorDomain::Vertex;
    };

    struct MeshSparseOperatorAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct MeshSparseOperatorHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    class MeshSparseOperatorAssetModule
    {
    public:
        MeshSparseOperatorAssetModule(
            wz::asset::AssetSystem& system,
            MeshSparseOperatorTable& table);

        [[nodiscard]] MeshSparseOperatorAsset create_sparse_operator(
            const MeshSparseOperatorDesc& desc);

        [[nodiscard]] MeshSparseOperatorHandle get_sparse_operator(
            const MeshSparseOperatorAsset& asset) const;

        [[nodiscard]] const MeshSparseOperatorData* get_sparse_operator_data(
            MeshSparseOperatorHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        MeshSparseOperatorTable& table_;
    };
}
