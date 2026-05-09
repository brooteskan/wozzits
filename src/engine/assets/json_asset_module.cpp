// src/engine/assets/json_asset_module.cpp

#include <engine/assets/json_asset_module.h>

#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/key_factories/json.h>

#include <vector>

namespace wz::engine::assets
{
    JSONAssetModule::JSONAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        FileCarrierAssetModule& files,
        JSONTable& table)
        : system_(system)
        , logger_(logger)
        , files_(files)
        , table_(table)
    {
    }

    JSONAsset JSONAssetModule::create_json(const JSONFileDesc& desc)
    {
        const wz::asset::AssetKey file_key = files_.register_file_node(
            desc.path,
            kTextFileSchema,
            kAssetTypeTextFile
        );

        if (file_key == wz::asset::AssetKey{}) {
            logger_.error("failed to register JSON source file: " + desc.path);
            return {};
        }

        const wz::asset::AssetKey json_key =
            make_json_document_key(file_key);

        wz::asset::AssetNode node;
        node.key = json_key;
        node.type = kAssetTypeJSONDocument;
        node.schema = kJSONDocumentSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};

        if (!system_.register_asset(std::move(node), { file_key })) {
            logger_.error("failed to register JSON document node: " + desc.name);
            return {};
        }

        return JSONAsset{ .output = json_key };
    }

    JSONHandle JSONAssetModule::get_json(const JSONAsset& asset) const
    {
        if (!asset.valid())
            return {};

        JSONHandle out{};

        if (const auto* compiled = system_.find_compiled(asset.output))
            out.handle = compiled->handle;

        if (!out.valid())
            logger_.error("JSON handle not found");

        return out;
    }

    const JSONData* JSONAssetModule::get_json_data(JSONHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return table_.get(handle.handle);
    }

} // namespace wz::engine::assets