#pragma once

// engine/assets/atmosphere/atmosphere_compilers.h

#include <asset/compiler.h>
#include <engine/assets/atmosphere/atmosphere.h>

#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    // Registers the params-only Atmosphere compiler (kAtmosphereSchema ->
    // kAssetTypeAtmosphere). Atmosphere has no input ports: the whole fog state
    // is authored, like the Placement recipe, so the compiler reads its dials
    // out of the node's ParamBlock (or a typed AtmosphereCompileDesc) and
    // stores the result.
    void register_atmosphere_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        AtmosphereTable& atmosphere_table);
}
