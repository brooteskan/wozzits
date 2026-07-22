// src/engine/assets/puppet_asset_module.cpp

#include <engine/assets/puppet_asset_module.h>

#include <engine/assets/key_factories/puppet.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <vector>

namespace wz::engine::assets
{
    PuppetAssetModule::PuppetAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        PuppetTable& table)
        : system_(system)
        , logger_(logger)
        , table_(table)
    {
    }

    PuppetAsset PuppetAssetModule::create_puppet_from_file(
        const PuppetFromFileDesc& desc)
    {
        PuppetAsset out{};

        if (desc.source_file == wz::asset::AssetKey{}) {
            logger_.error("puppet from file has empty source file key: " + desc.name);
            return out;
        }

        const wz::asset::AssetKey key =
            make_puppet_from_file_key(desc.source_file);

        wz::asset::AssetNode node{};
        node.key = key;
        node.type = kAssetTypePuppet;
        node.schema = kPuppetFromFileSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};

        if (!system_.register_asset(std::move(node), { desc.source_file })) {
            logger_.error("failed to register puppet from file: " + desc.name);
            return out;
        }

        out.output = key;
        return out;
    }
}
