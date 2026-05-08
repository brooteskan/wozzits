// src/engine/assets/csv_asset_module.cpp

#include <engine/assets/csv_asset_module.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/key_factories/csv.h>

#include <vector>

namespace wz::engine::assets
{

    CSVAssetModule::CSVAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger&             logger,
        FileCarrierAssetModule& files,
        CSVTable&               table)
        : system_(system)
        , logger_(logger)
        , files_(files)
        , table_(table)
    {
    }

    CSVAsset CSVAssetModule::create_csv(const CSVFileDesc& desc)
    {
        const wz::asset::AssetKey file_key = files_.register_file_node(
            desc.path, kCSVFileSchema, kAssetTypeRawFile);

        if (file_key == wz::asset::AssetKey{}) {
            logger_.error("failed to register CSV source file: " + desc.path);
            return {};
        }

        const wz::asset::AssetKey csv_key = make_csv_key(
            file_key,
            static_cast<uint8_t>(desc.header_mode)
        );

        CSVCompileDesc compile_desc{ .header_mode = desc.header_mode };

        wz::asset::AssetNode node;
        node.key     = csv_key;
        node.type    = kAssetTypeCSVTable;
        node.schema  = kCSVTableSchema;
        node.stage   = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta    = compile_desc;

        if (!system_.register_asset(std::move(node), { file_key })) {
            logger_.error("failed to register CSV table node: " + desc.name);
            return {};
        }

        return CSVAsset{ .output = csv_key };
    }

    CSVHandle CSVAssetModule::get_csv(const CSVAsset& asset) const
    {
        if (!asset.valid())
            return {};

        CSVHandle out{};
        if (const auto* compiled = system_.find_compiled(asset.output))
            out.handle = compiled->handle;

        if (!out.valid())
            logger_.error("CSV handle not found");

        return out;
    }

    const CSVData* CSVAssetModule::get_csv_data(CSVHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return table_.get(handle.handle);
    }

} // namespace wz::engine::assets
