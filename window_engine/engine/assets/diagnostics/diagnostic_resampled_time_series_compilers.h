#pragma once

// engine/assets/diagnostics/diagnostic_resampled_time_series_compilers.h

#include <asset/compiler.h>
#include <engine/assets/diagnostics/diagnostic_resampled_time_series.h>
#include <engine/assets/diagnostics/diagnostic_table.h>
#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    void register_diagnostic_resampled_time_series_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        DiagnosticTable& diagnostic_table,
        DiagnosticResampledTimeSeriesTable& resampled_table);
}