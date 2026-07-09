#pragma once

// engine/assets/data_table_csv_export.h
//
// One home for the data_table -> csv_export -> file plumbing. Given a built
// DataTableData and a destination path, it registers the two inline asset nodes,
// commits + resolves them, and writes the resolved CSV. Extracted from
// WozzitsApp_v1's frame-profiler flush (#258 avenue 5) so any host or tool that
// wants to dump a table to CSV shares this ~40-line asset-chain dance instead of
// re-inlining it. The CALLER owns the table schema (columns + rows) and the
// destination path; this owns only the export mechanism.

#include <engine/assets/data_table/data_table.h>   // DataTableData

#include <file/filesystem.h>

#include <string_view>

namespace wz::engine::assets
{
    class EngineAssetLibrary;

    // Which stage of write_data_table_to_csv_file failed (or Ok). Returned rather
    // than logged so the caller can map each stage to its own message and keep the
    // helper free of any host-specific diagnostics.
    enum class DataTableCsvExportStatus
    {
        Ok,
        TableInvalid,      // create_inline_table produced an invalid asset
        ExportInvalid,     // create_csv_export produced an invalid asset
        ExportUnresolved,  // the export did not resolve after commit/resolve_all
        WriteFailed,       // the resolved export could not be written to `path`
    };

    // Write `table` to `path` as CSV through the data_table -> csv_export asset
    // chain: register an inline table node named `name` and an export node named
    // `<name>_csv`, commit + resolve_all() (cache-hit for the rest of the graph,
    // so only these two nodes do real work), then write the resolved export to
    // `path`. `table` is consumed. Does no logging — see DataTableCsvExportStatus.
    DataTableCsvExportStatus write_data_table_to_csv_file(
        EngineAssetLibrary& assets,
        std::string_view name,
        DataTableData table,
        const wz::fs::Path& path);
}
