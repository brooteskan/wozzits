// src/engine/assets/csv_export_asset_module.cpp

#include <engine/assets/csv_export_asset_module.h>

#include <engine/assets/key_factories/csv_export.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <vector>

namespace wz::engine::assets
{
    CSVExportAssetModule::CSVExportAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        CSVExportTable& table)
        : system_(system)
        , logger_(logger)
        , table_(table)
    {
    }

    CSVExportAsset CSVExportAssetModule::create_csv_export(
        const CSVExportDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("csv export has empty name");
            return {};
        }

        if (!desc.source.valid()) {
            logger_.error("csv export has invalid source: " + desc.name);
            return {};
        }

        const wz::asset::AssetKey key = make_csv_export_key(
            desc.name,
            desc.source.output,
            desc.separator,
            desc.include_header);

        wz::asset::AssetNode node;
        node.key    = key;
        node.type   = kAssetTypeCSVExport;
        node.schema = kCSVExportSchema;
        node.stage  = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = CSVExportCompileDesc{
            .separator      = desc.separator,
            .include_header = desc.include_header,
        };

        if (!system_.register_asset(std::move(node), { desc.source.output })) {
            logger_.error("failed to register csv export: " + desc.name);
            return {};
        }

        return CSVExportAsset{ .output = key };
    }

    CSVExportHandle CSVExportAssetModule::get_export(
        const CSVExportAsset& asset) const
    {
        if (!asset.valid())
            return {};

        CSVExportHandle out{};

        if (const auto* compiled = system_.find_compiled(asset.output))
            out.handle = compiled->handle;

        if (!out.valid())
            logger_.error("csv export handle not found");

        return out;
    }

    const CSVExportData* CSVExportAssetModule::get_export_data(
        CSVExportHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return table_.get(handle.handle);
    }

    wz::fs::FileError CSVExportAssetModule::write_export_to_file(
        CSVExportHandle handle,
        const wz::fs::Path& path) const
    {
        if (!handle.valid())
            return wz::fs::FileError::InvalidArgument;

        const CSVExportData* data = table_.get(handle.handle);

        if (!data)
            return wz::fs::FileError::InvalidArgument;

        return wz::fs::write_file_text(path, data->csv_text);
    }
}
