#pragma once

// engine/assets/render_binding_layout_asset_module.h

#include <asset/system.h>

#include <engine/assets/render_binding_layout/render_binding_layout.h>

#include <logging/logger.h>

#include <string>

namespace wz::engine::assets
{
    struct RenderBindingLayoutDesc
    {
        std::string name;
        RenderBindingLayoutData layout{};
    };

    struct RenderBindingLayoutAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct RenderBindingLayoutHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept { return handle.valid(); }
    };

    class RenderBindingLayoutAssetModule
    {
    public:
        RenderBindingLayoutAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            RenderBindingLayoutTable& table);

        RenderBindingLayoutAsset create_render_binding_layout(
            const RenderBindingLayoutDesc& desc);

        RenderBindingLayoutHandle get_render_binding_layout(
            const RenderBindingLayoutAsset& asset) const;

        const RenderBindingLayoutData* get_render_binding_layout_data(
            RenderBindingLayoutHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        RenderBindingLayoutTable& table_;
    };
}
