#pragma once
// window_engine/engine/assets/mesh_asset_module.h

#include <asset/system.h>
#include <engine/assets/mesh/mesh.h>

#include <cstdint>
#include <string>

namespace wz::engine::assets
{
    enum class ProceduralMeshKind
    {
        Triangle,
        Quad,
        Cube,
    };

    struct ProceduralMeshDesc
    {
        std::string name;
        ProceduralMeshKind kind = ProceduralMeshKind::Triangle;
    };

    struct MeshAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct MeshHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    struct GLBMeshDesc
    {
        std::string name;
        wz::asset::AssetKey source_file;
        uint32_t mesh_index = 0;
    };

    struct MeshDecimationAssetDesc
    {
        std::string name;
        MeshAsset source_mesh;

        uint32_t target_vertex_count = 0;
        uint32_t target_triangle_count = 0;
        float target_ratio = 0.0f;

        bool preserve_boundary = true;

        float aspect_ratio = 0.0f;
        float edge_length = 0.0f;
        uint32_t max_valence = 0;
        float normal_deviation = 0.0f;
        float hausdorff_error = 0.0f;
    };

    class MeshAssetModule
    {
    public:
        explicit MeshAssetModule(
            wz::asset::AssetSystem& system,
            MeshTable& table);

        [[nodiscard]] MeshAsset create_procedural_mesh(
            const ProceduralMeshDesc& desc);

        [[nodiscard]] MeshAsset create_glb_mesh(
            const GLBMeshDesc& desc);

        [[nodiscard]] MeshAsset create_decimated_mesh(
            const MeshDecimationAssetDesc& desc);

        [[nodiscard]] MeshAsset create_placeholder_mesh(
            std::string name = {});

        [[nodiscard]] MeshHandle get_mesh(
            const MeshAsset& asset) const;

        [[nodiscard]] const MeshData* get_mesh_data(
            MeshHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        MeshTable& table_;
    };
}
