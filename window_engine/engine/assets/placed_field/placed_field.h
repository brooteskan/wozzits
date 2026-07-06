#pragma once

// engine/assets/placed_field/placed_field.h
//
// Runtime data types and table for the PlacedField asset (issue #223).
//
// A PlacedField is a thin combiner: it binds a frame-less data FIELD (a scalar
// field in v1) to a world-space Placement frame, producing a single shared
// upstream that many consumers descend from. Place the field ONCE, and every
// consumer (clipmap visual, terrain collision, a field preview) reads the same
// frame — instead of each consumer carrying its own Placement port that can
// drift out of alignment (issue #218's motivating footgun).
//
// It is a REFERENCE PAIR, not a data copy: it stores the field's AssetKey and
// the placement's AssetKey, never the field's (large, GPU-resident) bytes. This
// is deliberate — folding a Placement into the field's own identity would
// re-key + re-upload the field texture on every placement move and kill field
// reuse across placements (issue #218). The combiner keeps the raw field pure
// and the placement cheap to edit; a placement change re-keys only this cheap
// ref-pair (and its consumers), never the field.
//
// ── Ownership model ───────────────────────────────────────────────────────────
//
// The AssetSystem stores a ResourceHandle in compiled node payloads. The actual
// PlacedFieldData lives in PlacedFieldTable, owned by EngineAssetLibrary.
// Mirrors PlacementTable / ScalarFieldTable exactly.

#include <asset/types.h>

#include <cstdint>
#include <vector>

namespace wz::engine::assets
{
    // ─── PlacedFieldData ────────────────────────────────────────────────────────
    //
    // Immutable ref-pair: the frame-less field and the world frame it sits in.
    // field_type records the field's asset type (kAssetTypeScalarField in v1) so
    // a consumer resolving this can route the field to the right table without
    // re-deriving it; the pair generalises to vector fields / splats later by
    // widening the accepted field_type (issue #223 open Q3).

    struct PlacedFieldData
    {
        wz::asset::AssetKey  field_key{};      // the frame-less field (scalar field in v1)
        wz::asset::AssetKey  placement_key{};  // the world-space Placement frame
        wz::asset::AssetType field_type =
            wz::asset::AssetType::Unknown;      // the field's asset type (self-describing)

        bool valid() const noexcept
        {
            return !(field_key == wz::asset::AssetKey{})
                && !(placement_key == wz::asset::AssetKey{});
        }
    };


    // ─── PlacedFieldTable ─────────────────────────────────────────────────────────
    //
    // Runtime owner of resolved PlacedField ref-pairs. The AssetSystem stores only
    // a ResourceHandle; the actual PlacedFieldData lives in this table.
    //
    // V1: append-only. Epoch starts at 1 — epoch 0 in a ResourceHandle always
    // indicates invalid. Follows the PlacementTable / ScalarFieldTable shape.

    class PlacedFieldTable
    {
    public:
        // Reserves slot 0 as the invalid sentinel so all real handles have id >= 1.
        PlacedFieldTable();

        // Store a resolved ref-pair and return a handle to it.
        wz::asset::ResourceHandle add(PlacedFieldData placed_field);

        // Look up a ref-pair by handle. Returns nullptr if the handle is stale or
        // out of range. Const — does not modify the table.
        const PlacedFieldData* get(wz::asset::ResourceHandle handle) const;

        // Release all slots. Invalidates all outstanding handles.
        void destroy();

    private:
        struct Slot
        {
            uint32_t        epoch = 0;
            bool            occupied = false;
            PlacedFieldData placed_field;
        };

        std::vector<Slot> slots_;
    };
}
