#pragma once

// engine/assets/diagnostics/diagnostic_timeframe_summary_compilers.h

#include <asset/compiler.h>
#include <engine/assets/diagnostics/diagnostic_timeframe_summary.h>
#include <engine/assets/data_table/data_table.h>
#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    void register_diagnostic_timeframe_summary_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        DataTable& data_table,
        DiagnosticTimeframeSummaryTable& summary_table);
}
