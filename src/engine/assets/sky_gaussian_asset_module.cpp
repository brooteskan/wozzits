// src/engine/assets/sky_gaussian_asset_module.cpp

#include <engine/assets/sky_gaussian_asset_module.h>

#include <engine/assets/key_factories/sky_gaussian_from_json.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <vector>

namespace wz::engine::assets
{
    SkyGaussianAssetModule::SkyGaussianAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        SkyGaussianTable& table)
        : system_(system)
        , logger_(logger)
        , table_(table)
    {
    }

    SkyGaussianAsset SkyGaussianAssetModule::create_from_json(
        const SkyGaussianFromJSONDesc& desc)
    {
        SkyGaussianAsset out{};

        if (desc.json_key == wz::asset::AssetKey{}) {
            logger_.error(
                "sky gaussian from JSON has empty JSON document key: "
                + desc.name);
            return out;
        }

        const wz::asset::AssetKey key =
            make_sky_gaussian_from_json_key(desc.json_key);

        wz::asset::AssetNode node{};
        node.key     = key;
        node.type    = kAssetTypeSkyGaussian;
        node.schema  = kSkyGaussianFromJSONSchema;
        node.stage   = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};

        if (!system_.register_asset(std::move(node), { desc.json_key })) {
            logger_.error(
                "failed to register sky gaussian from JSON: " + desc.name);
            return {};
        }

        out.output = key;
        return out;
    }

    SkyGaussianHandle SkyGaussianAssetModule::get_set(
        const SkyGaussianAsset& asset) const
    {
        if (!asset.valid())
            return {};

        SkyGaussianHandle out{};

        if (const auto* compiled = system_.find_compiled(asset.output))
            out.handle = compiled->handle;

        if (!out.valid())
            logger_.error("sky gaussian set handle not found");

        return out;
    }

    const sky::SkyGaussianSet* SkyGaussianAssetModule::get_set_data(
        SkyGaussianHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return table_.get(handle.handle);
    }
}
