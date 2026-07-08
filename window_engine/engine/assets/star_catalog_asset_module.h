#pragma once

// engine/assets/star_catalog_asset_module.h

#include <asset/system.h>
#include <asset/types.h>
#include <engine/starfield/star_catalog_table.h>
#include <logging/logger.h>

#include <string>

namespace wz::engine::assets
{
    struct StarCatalogAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct StarCatalogHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    // Star catalog compiled from a baked .star_catalog.json document. The
    // json_key must name a compiled JSONDocument (create it via
    // JSONAssetModule::create_json). Produces a kAssetTypeStarCatalog asset.
    struct StarCatalogFromJSONDesc
    {
        std::string         name;
        wz::asset::AssetKey json_key{};  // from JSONAsset::output
    };

    class StarCatalogAssetModule
    {
    public:
        StarCatalogAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            wz::engine::starfield::StarCatalogTable& table);

        StarCatalogAsset create_from_json(const StarCatalogFromJSONDesc& desc);

        StarCatalogHandle get_catalog(const StarCatalogAsset& asset) const;

        const wz::engine::starfield::StarCatalog* get_catalog_data(
            StarCatalogHandle handle) const;

    private:
        wz::asset::AssetSystem&                   system_;
        wz::Logger&                               logger_;
        wz::engine::starfield::StarCatalogTable&  table_;
    };
}
