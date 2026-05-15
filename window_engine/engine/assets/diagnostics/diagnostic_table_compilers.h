#pragma once

// engine/assets/diagnostics/diagnostic_table_compilers.h

#include <asset/compiler.h>
#include <engine/assets/diagnostics/diagnostic_table.h>
#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    void register_diagnostic_table_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        DiagnosticTable& table);
}