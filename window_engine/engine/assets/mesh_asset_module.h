#pragma once
// window_engine/engine/assets/mesh_asset_module.h

#include <asset/system.h>
#include <engine/assets/mesh/mesh.h>

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

    class MeshAssetModule
    {
    public:
        explicit MeshAssetModule(
            wz::asset::AssetSystem& system,
            MeshTable& table);

        [[nodiscard]] MeshAsset create_procedural_mesh(
            const ProceduralMeshDesc& desc);

        [[nodiscard]] MeshHandle get_mesh(
            const MeshAsset& asset) const;

        [[nodiscard]] const MeshData* get_mesh_data(
            MeshHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        MeshTable& table_;
    };
}