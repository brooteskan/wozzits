// src/engine/assets/data_table_csv_export.cpp

#include <engine/assets/data_table_csv_export.h>

#include <engine/assets/csv_export_asset_module.h>
#include <engine/assets/data_table_asset_module.h>
#include <engine/assets/engine_asset_library.h>

#include <string>
#include <utility>

namespace wz::engine::assets
{
    DataTableCsvExportStatus write_data_table_to_csv_file(
        EngineAssetLibrary& assets,
        std::string_view name,
        DataTableData table,
        const wz::fs::Path& path)
    {
        // Register the table as an inline data_table node, then an export node that
        // reads it. Built as a data_table asset and exported via csv_export -- the
        // same chain the diagnostic tables use.
        const DataTableAsset table_asset =
            assets.data_tables().create_inline_table(
                { .name = std::string(name), .table = std::move(table) });
        if (!table_asset.valid()) {
            return DataTableCsvExportStatus::TableInvalid;
        }

        const CSVExportAsset csv_asset =
            assets.csv_export().create_csv_export(
                { .name = std::string(name) + "_csv", .source = table_asset });
        if (!csv_asset.valid()) {
            return DataTableCsvExportStatus::ExportInvalid;
        }

        // Compile the two new nodes. The commit re-registers the graph, but resolve
        // is cache-hit for everything already resolved, so only these two do real
        // work.
        assets.commit();
        (void)assets.resolve_all();

        const CSVExportHandle handle = assets.csv_export().get_export(csv_asset);
        if (!handle.valid()) {
            return DataTableCsvExportStatus::ExportUnresolved;
        }
        if (assets.csv_export().write_export_to_file(handle, path)
            != wz::fs::FileError::None)
        {
            return DataTableCsvExportStatus::WriteFailed;
        }
        return DataTableCsvExportStatus::Ok;
    }
}
