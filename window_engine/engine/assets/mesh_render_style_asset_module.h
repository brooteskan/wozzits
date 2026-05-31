#pragma once

// engine/assets/mesh_render_style_asset_module.h

#include <asset/system.h>

#include <engine/assets/mesh_render_style/mesh_render_style.h>

#include <logging/logger.h>

#include <string>

namespace wz::engine::assets
{
    struct MeshRenderStyleDesc
    {
        std::string name;
        MeshRenderStyleData style{};
    };

    struct MeshRenderStyleAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct MeshRenderStyleHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept { return handle.valid(); }
    };

    class MeshRenderStyleAssetModule
    {
    public:
        MeshRenderStyleAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            MeshRenderStyleTable& table);

        MeshRenderStyleAsset create_mesh_render_style(
            const MeshRenderStyleDesc& desc);

        MeshRenderStyleHandle get_mesh_render_style(
            const MeshRenderStyleAsset& asset) const;

        const MeshRenderStyleData* get_mesh_render_style_data(
            MeshRenderStyleHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        MeshRenderStyleTable& table_;
    };
}
