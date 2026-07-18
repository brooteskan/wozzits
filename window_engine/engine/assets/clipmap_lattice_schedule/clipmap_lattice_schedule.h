#pragma once

// engine/assets/clipmap_lattice_schedule/clipmap_lattice_schedule.h
//
// Runtime data types and table for the ClipmapLatticeSchedule asset.
//
// A ClipmapLatticeSchedule is the RESOLVED geometry-clipmap LOD schedule: the
// {base_resolution, level_count, cell_size} lattice that resolve_clipmap_lattice
// derives from the authored dials (world extent, horizon, triangle budget) plus
// the height field's resolution N, together with the horizon that lattice
// actually reaches and its exact triangle count.
//
// Why an ASSET and not a function-local? Because the schedule has more than one
// reader. It was already computed inside the clipmap lattice MESH compiler and
// thrown away as a local, so nothing downstream could learn it — and the terrain
// collision, which must reconstruct the very same rings to agree with what the
// renderer draws, carried the numbers HAND-TYPED instead. Promoting the schedule
// to a first-class asset gives it ONE producer that every consumer descends
// from, so the visual lattice and its collision cannot disagree about where a
// LOD ring sits. Same shape of fix as PlacedField (issue #223) applied to the
// derived schedule rather than to the placement frame.
//
// The schedule is DERIVED, not authored: it stores only what the resolver
// returns. The height field it descends from supplies exactly one input, N (its
// texel count per side); the field's samples are never read, so a schedule is
// cheap to recompute and re-keys whenever the field's identity changes.
//
// ── Ownership model ───────────────────────────────────────────────────────────
//
// The AssetSystem stores a ResourceHandle in compiled node payloads. The actual
// ClipmapLatticeScheduleData lives in ClipmapLatticeScheduleTable, owned by
// EngineAssetLibrary. Mirrors PlacedFieldTable / PlacementTable exactly.

#include <asset/types.h>

#include <cstdint>
#include <vector>

namespace wz::engine::assets
{
    // ─── ClipmapLatticeScheduleData ──────────────────────────────────────────────
    //
    // Immutable, resolved LOD schedule. The first three fields ARE the geometric
    // lattice (they reproduce ClipmapLatticeParams exactly, so a consumer can
    // hand them straight to make_clipmap_lattice_mesh); achieved_horizon and
    // triangle_count are the resolver's report on what that lattice costs and
    // covers.

    struct ClipmapLatticeScheduleData
    {
        // m — the finest level's per-side resolution in cells.
        uint32_t base_resolution = 0u;
        // L — the number of nested LOD rings, level 0 (finest) included.
        uint32_t level_count = 0u;
        // c0 — metres per finest lattice cell. Equals metres per texel by
        // construction (the finest cell is one height-field texel).
        float cell_size = 0.0f;
        // Metres actually reached from the lattice centre by the coarsest ring.
        // Always >= the requested horizon; see resolve_clipmap_lattice.
        float achieved_horizon = 0.0f;
        // Exact triangle count of the whole lattice, not an estimate.
        uint64_t triangle_count = 0u;

        bool valid() const noexcept
        {
            return base_resolution > 0u
                && level_count > 0u
                && cell_size > 0.0f;
        }
    };


    // ─── ClipmapLatticeScheduleTable ─────────────────────────────────────────────
    //
    // Runtime owner of resolved schedules. The AssetSystem stores only a
    // ResourceHandle; the actual ClipmapLatticeScheduleData lives in this table.
    //
    // V1: append-only. Epoch starts at 1 — epoch 0 in a ResourceHandle always
    // indicates invalid. Follows the PlacedFieldTable / PlacementTable shape.

    class ClipmapLatticeScheduleTable
    {
    public:
        // Reserves slot 0 as the invalid sentinel so all real handles have id >= 1.
        ClipmapLatticeScheduleTable();

        // Store a resolved schedule and return a handle to it.
        wz::asset::ResourceHandle add(ClipmapLatticeScheduleData schedule);

        // Look up a schedule by handle. Returns nullptr if the handle is stale or
        // out of range. Const — does not modify the table.
        const ClipmapLatticeScheduleData* get(
            wz::asset::ResourceHandle handle) const;

        // Release all slots. Invalidates all outstanding handles.
        void destroy();

    private:
        struct Slot
        {
            uint32_t                   epoch = 0;
            bool                       occupied = false;
            ClipmapLatticeScheduleData schedule;
        };

        std::vector<Slot> slots_;
    };
}
