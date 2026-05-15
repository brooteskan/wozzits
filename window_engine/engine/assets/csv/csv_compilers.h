#pragma once

#include <asset/compiler.h>
#include <logging/logger.h>
#include <engine/assets/csv/csv.h>

namespace wz::engine::assets::internal {

    void register_csv_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        CSVTable& csv_table
    );

}
