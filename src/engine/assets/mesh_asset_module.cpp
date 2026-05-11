#include <engine/assets/mesh_asset_module.h>

namespace wz::engine::assets
{
    MeshAssetModule::MeshAssetModule(
        wz::asset::AssetSystem& system,
        MeshTable& table)
        : system_(system)
        , table_(table)
    {
    }

    MeshAsset MeshAssetModule::create_procedural_mesh(
        const ProceduralMeshDesc&)
    {
        return {};
    }

    MeshHandle MeshAssetModule::get_mesh(
        const MeshAsset&) const
    {
        return {};
    }

    const MeshData* MeshAssetModule::get_mesh_data(
        MeshHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return table_.get(handle.handle);
    }
}