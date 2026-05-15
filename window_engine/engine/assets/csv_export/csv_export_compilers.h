#pragma once

// engine/assets/csv_export/csv_export_compilers.h

#include <asset/compiler.h>
#include <engine/assets/csv_export/csv_export.h>
#include <engine/assets/data_table/data_table.h>
#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    void register_csv_export_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        DataTable& data_table,
        CSVExportTable& csv_export_table);
}
