#pragma once

// engine/assets/placed_field/placed_field_compilers.h

#include <asset/compiler.h>
#include <engine/assets/placed_field/placed_field.h>

#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    // Registers the PlacedField combiner compiler (kPlacedFieldSchema ->
    // kAssetTypePlacedField). Two required input ports — a frame-less field
    // (a scalar field in v1) and a Placement frame — bound into one ref-pair.
    void register_placed_field_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        PlacedFieldTable& placed_field_table);
}
