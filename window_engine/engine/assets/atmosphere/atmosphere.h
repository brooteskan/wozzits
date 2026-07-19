#pragma once

// engine/assets/atmosphere/atmosphere.h
//
// Runtime data types and table for the Atmosphere asset.
//
// An Atmosphere is the global fog for a frame: the medium every program looks
// THROUGH, carrying the fog colour, how thick it is, how far away it starts,
// how fast it thins with height, and whether it is on at all.
//
// Why an ASSET and not a dial on each renderable? Because fog is a property of
// the world and the observer, not of any one thing being drawn, so every
// program must see the SAME values or the scene disagrees with itself — terrain
// receding into a haze the splats standing on it never entered. The render
// binding layout already made room for exactly that: RenderBindingViewHead::
// CameraFog (a8d3450) is a VIEW-frequency root-constant block filled once per
// frame with the observer and the atmosphere it looks through, at a frequency
// no renderable owns. This asset is the single authorable PRODUCER of the
// atmosphere half of that block, so the numbers reaching every shader have one
// source. Same shape of fix as ClipmapLatticeSchedule (8284d8a) applied to fog
// instead of to a derived LOD schedule.
//
// Nothing consumes it yet: the renderer seam that fills the view constants from
// a resolved Atmosphere follows in its own commit. The asset is registered and
// authorable but inert here.
//
// A pure params recipe with NO input ports — the sibling of Placement rather
// than of ClipmapLatticeSchedule, which takes a scalar field. Fog is authored,
// never derived from upstream data.
//
// ── Ownership model ───────────────────────────────────────────────────────────
//
// The AssetSystem stores a ResourceHandle in compiled node payloads. The actual
// AtmosphereData lives in AtmosphereTable, owned by EngineAssetLibrary. Mirrors
// ClipmapLatticeScheduleTable / PlacementTable exactly.

#include <asset/types.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace wz::engine::assets
{
    // ─── AtmosphereData ─────────────────────────────────────────────────────────
    //
    // Immutable, CPU-side global fog state. The field order matches the c1/c2
    // rows of the CameraFog view head (fog_color.rgb, fog_density | start,
    // height_falloff, enabled) so a consumer can pack it without reshuffling.
    //
    // Defaults describe a pale daylight haze that is switched OFF: an atmosphere
    // nobody has authored must not silently tint a scene, so fog_enabled starts
    // false and fog_density starts 0. See valid() — "no fog" is a legitimate
    // atmosphere, not an unfinished one.

    struct AtmosphereData
    {
        // Linear RGB the fog converges to at full density.
        float fog_color[3] = { 0.7f, 0.7f, 0.7f };
        // Thickness per metre. 0 means fully transparent.
        float fog_density = 0.0f;
        // Metres from the observer before fog begins to accumulate.
        float fog_start_distance = 0.0f;
        // Rate the fog thins with world height. 0 means height-independent.
        float fog_height_falloff = 0.0f;
        // Master switch. Off means consumers skip the fog term entirely rather
        // than evaluating it with a zero density.
        bool fog_enabled = false;

        // An all-default atmosphere IS valid — it just means "no fog". This
        // rejects only values that could not describe a medium at all: a
        // negative density or start distance, or any non-finite number that
        // would poison the constants it is packed into. fog_enabled is
        // deliberately not a validity input; authoring the dials with the fog
        // switched off is the normal way to stage it.
        bool valid() const noexcept
        {
            return std::isfinite(fog_color[0])
                && std::isfinite(fog_color[1])
                && std::isfinite(fog_color[2])
                && std::isfinite(fog_density)
                && std::isfinite(fog_start_distance)
                && std::isfinite(fog_height_falloff)
                && fog_density >= 0.0f
                && fog_start_distance >= 0.0f;
        }
    };


    // ─── AtmosphereCompileDesc ──────────────────────────────────────────────────
    //
    // Stored in AssetNode::meta for Atmosphere compile nodes. The compiler builds
    // AtmosphereData from these parameters alone — Atmosphere has no
    // dependencies.
    //
    // name is intentionally absent: it is an identity input to the key factory
    // (so differently-named atmospheres with identical dials are distinct
    // assets), but the compiler does not need it to build the fog state.

    struct AtmosphereCompileDesc
    {
        float fog_color[3] = { 0.7f, 0.7f, 0.7f };
        float fog_density = 0.0f;
        float fog_start_distance = 0.0f;
        float fog_height_falloff = 0.0f;
        bool  fog_enabled = false;
    };


    // ─── AtmosphereTable ────────────────────────────────────────────────────────
    //
    // Runtime owner of resolved atmospheres. The AssetSystem stores only a
    // ResourceHandle; the actual AtmosphereData lives in this table.
    //
    // V1: append-only. Epoch starts at 1 — epoch 0 in a ResourceHandle always
    // indicates invalid. Follows the ClipmapLatticeScheduleTable / PlacementTable
    // shape.

    class AtmosphereTable
    {
    public:
        // Reserves slot 0 as the invalid sentinel so all real handles have id >= 1.
        AtmosphereTable();

        // Store a resolved atmosphere and return a handle to it.
        wz::asset::ResourceHandle add(AtmosphereData atmosphere);

        // Look up an atmosphere by handle. Returns nullptr if the handle is stale
        // or out of range. Const — does not modify the table.
        const AtmosphereData* get(wz::asset::ResourceHandle handle) const;

        // Release all slots. Invalidates all outstanding handles.
        void destroy();

    private:
        struct Slot
        {
            uint32_t       epoch = 0;
            bool           occupied = false;
            AtmosphereData atmosphere;
        };

        std::vector<Slot> slots_;
    };
}
