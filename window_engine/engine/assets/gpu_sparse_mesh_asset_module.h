#pragma once

// engine/assets/gpu_sparse_mesh_asset_module.h

#include <asset/system.h>
#include <engine/assets/gpu_sparse_mesh/gpu_sparse_mesh.h>
#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/mesh_sparse_operator_asset_module.h>

#include <string>

namespace wz::engine::assets
{
    struct GpuSparseMeshDesc
    {
        std::string name;
        MeshAsset source_mesh;
        MeshSparseOperatorKind operator_kind =
            MeshSparseOperatorKind::UniformVertexLaplacian;
        MeshOperatorDomain operator_domain = MeshOperatorDomain::Vertex;
    };

    struct GpuSparseMeshAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct GpuSparseMeshHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    class GpuSparseMeshAssetModule
    {
    public:
        GpuSparseMeshAssetModule(
            wz::asset::AssetSystem& system,
            GpuSparseMeshTable& table);

        [[nodiscard]] GpuSparseMeshAsset create_gpu_sparse_mesh(
            const GpuSparseMeshDesc& desc);

        [[nodiscard]] GpuSparseMeshHandle get_gpu_sparse_mesh(
            const GpuSparseMeshAsset& asset) const;

        [[nodiscard]] const GpuSparseMeshData* get_gpu_sparse_mesh_data(
            GpuSparseMeshHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        GpuSparseMeshTable& table_;
    };
}
