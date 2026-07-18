#pragma once

// engine/assets/clipmap_lattice_schedule/clipmap_lattice_schedule_compilers.h

#include <asset/compiler.h>
#include <engine/assets/clipmap_lattice_schedule/clipmap_lattice_schedule.h>
#include <engine/assets/scalar_field/scalar_field.h>

#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    // Registers the ClipmapLatticeSchedule compiler
    // (kClipmapLatticeScheduleSchema -> kAssetTypeClipmapLatticeSchedule). One
    // required input port — the height field, which supplies N — plus the
    // authored world_extent / horizon / triangle_budget dials; the compiler runs
    // resolve_clipmap_lattice over them and stores the resolved schedule.
    void register_clipmap_lattice_schedule_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        ScalarFieldTable& scalar_field_table,
        ClipmapLatticeScheduleTable& clipmap_lattice_schedule_table);
}
