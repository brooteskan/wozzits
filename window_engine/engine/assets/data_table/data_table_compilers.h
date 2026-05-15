#pragma once

// engine/assets/data_table/data_table_compilers.h

#include <asset/compiler.h>
#include <engine/assets/data_table/data_table.h>
#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    void register_data_table_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        DataTable& table);
}
