#pragma once

#include <asset/compiler.h>
#include <logging/logger.h>
#include <engine/assets/toml/toml.h>

namespace wz::engine::assets::internal {

    void register_toml_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        TOMLTable& toml_table
    );

}
