// src/engine/assets/star_catalog_asset_module.cpp

#include <engine/assets/star_catalog_asset_module.h>

#include <engine/assets/key_factories/star_catalog_from_json.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <vector>

namespace wz::engine::assets
{
    StarCatalogAssetModule::StarCatalogAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        wz::engine::starfield::StarCatalogTable& table)
        : system_(system)
        , logger_(logger)
        , table_(table)
    {
    }

    StarCatalogAsset StarCatalogAssetModule::create_from_json(
        const StarCatalogFromJSONDesc& desc)
    {
        StarCatalogAsset out{};

        if (desc.json_key == wz::asset::AssetKey{}) {
            logger_.error(
                "star catalog from JSON has empty JSON document key: "
                + desc.name);
            return out;
        }

        const wz::asset::AssetKey key =
            make_star_catalog_from_json_key(desc.json_key);

        wz::asset::AssetNode node{};
        node.key     = key;
        node.type    = kAssetTypeStarCatalog;
        node.schema  = kStarCatalogFromJSONSchema;
        node.stage   = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};

        if (!system_.register_asset(std::move(node), { desc.json_key })) {
            logger_.error(
                "failed to register star catalog from JSON: " + desc.name);
            return {};
        }

        out.output = key;
        return out;
    }

    StarCatalogHandle StarCatalogAssetModule::get_catalog(
        const StarCatalogAsset& asset) const
    {
        if (!asset.valid())
            return {};

        StarCatalogHandle out{};

        if (const auto* compiled = system_.find_compiled(asset.output))
            out.handle = compiled->handle;

        if (!out.valid())
            logger_.error("star catalog handle not found");

        return out;
    }

    const wz::engine::starfield::StarCatalog*
    StarCatalogAssetModule::get_catalog_data(StarCatalogHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return table_.get(handle.handle);
    }
}
