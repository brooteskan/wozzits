#pragma once

#include <asset/compiler.h>
#include <logging/logger.h>

#include <engine/assets/light/light.h>

namespace wz::engine::assets::internal
{
    void register_light_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        DirectLightTable& direct_light_table,
        AmbientLightingTable& ambient_lighting_table);
}
