#pragma once

// engine/assets/placement/placement.h
//
// Runtime data types and table for the Placement asset.
//
// A Placement is a generic, immutable CPU-side world-space frame: where a piece
// of data sits in the world and how large it is. It carries NO terrain /
// heightfield / texel vocabulary — it is a pure frame that any consumer can
// interpret. The clipmap (Phase 2) derives metres-per-texel (c0) from
// extent / field_dims at consume time; the frame itself stores world extent,
// never a derived per-texel quantity.
//
// ── Ownership model ───────────────────────────────────────────────────────────
//
// The AssetSystem stores a ResourceHandle in compiled node payloads. The actual
// PlacementData lives in PlacementTable, owned by EngineAssetLibrary.
// PlacementTable::add() stores the data and returns the handle;
// PlacementTable::get() resolves it back. Mirrors ScalarFieldTable /
// CollisionAssetTable exactly.

#include <asset/types.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace wz::engine::assets
{
    // ─── PlacementData ──────────────────────────────────────────────────────────
    //
    // Immutable, CPU-side world-space frame. origin is the world position of the
    // referenced data's (0,0) corner; origin[1] is the base world Y and mirrors
    // base_height. extent is the world footprint: extent.xz is the horizontal
    // span (metres), extent.y is the vertical scale (world Y per unit field
    // value). No metres-per-texel is stored here — that is derived downstream.

    struct PlacementData
    {
        float origin[3]   = { 0.0f, 0.0f, 0.0f };  // world (0,0) corner; origin[1]==base_height
        float extent[3]   = { 1.0f, 1.0f, 1.0f };  // footprint X/Z; extent[1]=vertical_scale
        float base_height = 0.0f;                   // world Y of field value 0 (mirror of origin[1])

        bool valid() const noexcept
        {
            // origin/base_height were unchecked, so a non-finite Placement
            // passed the compiler's ONLY gate and reached both consumers with
            // opposite outcomes. Require the whole datum finite. (C1-C65, #314)
            return std::isfinite(extent[0])
                && std::isfinite(extent[1])
                && std::isfinite(extent[2])
                && std::isfinite(origin[0])
                && std::isfinite(origin[1])
                && std::isfinite(origin[2])
                && std::isfinite(base_height);
        }
    };


    // ─── PlacementCompileDesc ─────────────────────────────────────────────────────
    //
    // Stored in AssetNode::meta for Placement compile nodes. The compiler builds
    // PlacementData from these parameters alone — Placement has no dependencies.
    //
    // name is intentionally absent: it is an identity input to the key factory
    // (so differently-named frames with identical parameters are distinct assets),
    // but the compiler does not need it to build the frame.

    struct PlacementCompileDesc
    {
        // origin[1] is ignored: the compiler always sets PlacementData::origin[1]
        // to base_height so the two stay a single source of truth. Author the
        // vertical position through base_height.
        float origin[3]   = { 0.0f, 0.0f, 0.0f };
        float extent[3]   = { 1.0f, 1.0f, 1.0f };
        float base_height = 0.0f;
    };


    // ─── PlacementTable ───────────────────────────────────────────────────────────
    //
    // Runtime owner of resolved Placement frames. The AssetSystem stores only a
    // ResourceHandle; the actual PlacementData lives in this table.
    //
    // V1: append-only. Epoch starts at 1 — epoch 0 in a ResourceHandle always
    // indicates invalid. Follows the ScalarFieldTable / CollisionAssetTable shape.

    class PlacementTable
    {
    public:
        // Reserves slot 0 as the invalid sentinel so all real handles have id >= 1.
        PlacementTable();

        // Store a resolved frame and return a handle to it.
        wz::asset::ResourceHandle add(PlacementData placement);

        // Look up a frame by handle. Returns nullptr if the handle is stale or
        // out of range. Const — does not modify the table.
        const PlacementData* get(wz::asset::ResourceHandle handle) const;

        // Release all slots. Invalidates all outstanding handles.
        void destroy();

    private:
        struct Slot
        {
            uint32_t      epoch = 0;
            bool          occupied = false;
            PlacementData placement;
        };

        std::vector<Slot> slots_;
    };
}
