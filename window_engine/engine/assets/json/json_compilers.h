#pragma once

#include <asset/compiler.h>
#include <logging/logger.h>
#include <engine/assets/json/json.h>

namespace wz::engine::assets::internal {

    void register_json_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        JSONTable& json_table
    );

}
