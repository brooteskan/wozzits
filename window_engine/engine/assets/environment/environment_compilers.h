#pragma once

// engine/assets/environment/environment_compilers.h

#include <asset/compiler.h>
#include <engine/assets/environment/environment.h>

#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    // Registers the FrameEnvironment compiler (kFrameEnvironmentSchema ->
    // kAssetTypeFrameEnvironment). FrameEnvironment is a pure AGGREGATOR: it takes
    // up to four OPTIONAL input ports (atmosphere, ambient lighting, HDRI
    // environment, directional light), records the connected pieces' keys into an
    // EnvironmentData bundle, and stores it. Ports are located by asset type, not
    // position, so any subset (including none) compiles.
    void register_environment_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        EnvironmentTable& environment_table);
}
