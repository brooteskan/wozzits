#pragma once

#include <asset/system.h>
#include <asset/types.h>
#include <engine/assets/terrain_asset_module.h>
#include <engine/assets/terrain/terrain_visual_proxy.h>
#include <logging/logger.h>

#include <string>

namespace wz::engine::assets
{
    struct TerrainVisualProxyAsset
    {
        wz::asset::AssetKey output{};

        [[nodiscard]] bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct TerrainVisualProxyHandle
    {
        wz::asset::ResourceHandle handle{};

        [[nodiscard]] bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    struct TerrainVisualProxyDesc
    {
        std::string name;
        TerrainAsset terrain;
    };

    class TerrainVisualProxyAssetModule
    {
    public:
        TerrainVisualProxyAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            TerrainVisualProxyTable& table);

        [[nodiscard]] TerrainVisualProxyAsset create_from_terrain(
            const TerrainVisualProxyDesc& desc);

        [[nodiscard]] TerrainVisualProxyHandle get_proxy(
            const TerrainVisualProxyAsset& asset) const;

        [[nodiscard]] const TerrainVisualProxyData* get_proxy_data(
            TerrainVisualProxyHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        TerrainVisualProxyTable& table_;
    };
}
