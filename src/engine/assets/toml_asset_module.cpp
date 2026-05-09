// src/engine/assets/toml_asset_module.cpp

#include <engine/assets/toml_asset_module.h>

#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/key_factories/toml.h>

#include <vector>

namespace wz::engine::assets
{
    TOMLAssetModule::TOMLAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        FileCarrierAssetModule& files,
        TOMLTable& table)
        : system_(system)
        , logger_(logger)
        , files_(files)
        , table_(table)
    {
    }

    TOMLAsset TOMLAssetModule::create_toml(const TOMLFileDesc& desc)
    {
        const wz::asset::AssetKey file_key = files_.register_file_node(
            desc.path,
            kTextFileSchema,
            kAssetTypeTextFile
        );

        if (file_key == wz::asset::AssetKey{}) {
            logger_.error("failed to register TOML source file: " + desc.path);
            return {};
        }

        const wz::asset::AssetKey toml_key =
            make_toml_document_key(file_key);

        wz::asset::AssetNode node;
        node.key = toml_key;
        node.type = kAssetTypeTOMLDocument;
        node.schema = kTOMLDocumentSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};

        if (!system_.register_asset(std::move(node), { file_key })) {
            logger_.error("failed to register TOML document node: " + desc.name);
            return {};
        }

        return TOMLAsset{ .output = toml_key };
    }

    TOMLHandle TOMLAssetModule::get_toml(const TOMLAsset& asset) const
    {
        if (!asset.valid())
            return {};

        TOMLHandle out{};

        if (const auto* compiled = system_.find_compiled(asset.output))
            out.handle = compiled->handle;

        if (!out.valid())
            logger_.error("TOML handle not found");

        return out;
    }

    const TOMLData* TOMLAssetModule::get_toml_data(TOMLHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return table_.get(handle.handle);
    }

} // namespace wz::engine::assets