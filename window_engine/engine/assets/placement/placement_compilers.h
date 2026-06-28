#pragma once

// engine/assets/placement/placement_compilers.h

#include <asset/compiler.h>
#include <engine/assets/placement/placement.h>

#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    // Registers the params-only Placement compiler (kPlacementSchema ->
    // kAssetTypePlacement). Placement has no input ports: it is a pure
    // authored-params source, like the procedural scalar field recipe.
    void register_placement_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        PlacementTable& placement_table);
}
