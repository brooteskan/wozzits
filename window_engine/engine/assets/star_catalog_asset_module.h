#pragma once

// engine/assets/star_catalog_asset_module.h

#include <asset/system.h>
#include <asset/types.h>
#include <engine/assets/star_catalog/star_catalog_table.h>
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

    // Star catalog imported from a baked PLY point cloud (tycho2_prep output).
    // source_file names a kAssetTypeRawFile (register it via
    // FileCarrierAssetModule::register_file_node). The creative dials ride the
    // desc and fold into the asset key, so re-tuning re-imports.
    struct StarCatalogFromPLYDesc
    {
        std::string                            name;
        wz::asset::AssetKey                    source_file{};
        wz::engine::starfield::StarImportParams params{};
    };

    class StarCatalogAssetModule
    {
    public:
        StarCatalogAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            wz::engine::starfield::StarCatalogTable& table);

        StarCatalogAsset create_from_json(const StarCatalogFromJSONDesc& desc);

        StarCatalogAsset create_from_ply(const StarCatalogFromPLYDesc& desc);

        StarCatalogHandle get_catalog(const StarCatalogAsset& asset) const;

        const wz::engine::starfield::StarCatalog* get_catalog_data(
            StarCatalogHandle handle) const;

    private:
        wz::asset::AssetSystem&                   system_;
        wz::Logger&                               logger_;
        wz::engine::starfield::StarCatalogTable&  table_;
    };
}
