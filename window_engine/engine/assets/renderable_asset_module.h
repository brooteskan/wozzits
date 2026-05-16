#pragma once

// engine/assets/renderable_asset_module.h

#include <asset/system.h>

#include <engine/assets/renderable/renderable.h>
#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/gaussian_splat_asset_module.h>

#include <logging/logger.h>

#include <string>

namespace wz::engine::assets
{
    struct MeshWireframeRenderableDesc
    {
        std::string name;
        MeshAsset mesh{};
    };

    struct GaussianSplatDebugRenderableDesc
    {
        std::string name;
        GaussianSplatCloudAsset splat_cloud{};
    };

    struct RenderableAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct RenderableHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    class RenderableAssetModule
    {
    public:
        RenderableAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            RenderableAssetTable& table);

        RenderableAsset create_mesh_wireframe(
            const MeshWireframeRenderableDesc& desc);

        RenderableAsset create_gaussian_splat_debug(
            const GaussianSplatDebugRenderableDesc& desc);

        RenderableHandle get_renderable(
            const RenderableAsset& asset) const;

        const RenderableAssetData* get_renderable_data(
            RenderableHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        RenderableAssetTable& table_;
    };
}