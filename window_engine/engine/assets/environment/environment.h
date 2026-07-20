#pragma once

// engine/assets/environment/environment.h
//
// Runtime data types and table for the FrameEnvironment asset.
//
// A FrameEnvironment is the single CONNECTED producer of a frame's global
// environment: the atmosphere every program looks through, the ambient light,
// the image-based (HDRI) environment, and the global directional (sun) light.
// It exists to end the "environment island" problem — each of these was an
// unconnected asset-graph node reached only through a scattered per-node scene
// component, so nothing in the graph showed which state the frame is actually
// built from. FrameEnvironment gathers them by EDGE: the pieces feed its input
// ports, one scene component references it, and the renderer resolves that one
// node.
//
// A pure AGGREGATOR — it owns no dials of its own beyond a name. Every port is
// OPTIONAL and located by asset type (dep_key_of_type), so any subset (including
// none) is a valid environment. Sky is deliberately NOT a port yet: sky_visual /
// sky_surface are inline scene components, not asset-graph nodes, so there is
// nothing to point an edge at until sky is promoted to an asset (its own seam).
//
// ── Ownership model ───────────────────────────────────────────────────────────
//
// The AssetSystem stores a ResourceHandle in compiled node payloads. The actual
// EnvironmentData lives in EnvironmentTable, owned by EngineAssetLibrary. Mirrors
// AtmosphereTable / ClipmapLatticeScheduleTable exactly.

#include <asset/types.h>

#include <cstdint>
#include <vector>

namespace wz::engine::assets
{
    // ─── EnvironmentData ─────────────────────────────────────────────────────────
    //
    // Immutable, CPU-side bundle of the frame-global environment pieces, each held
    // BY KEY (the resolved AssetKey of the connected input). A consumer resolves
    // each key through the piece's own module/table — FrameEnvironment does not
    // copy or interpret the pieces, only names which ones the frame is built from.
    //
    // An empty key means that role is unbound, exactly as if the port were
    // unconnected. An all-empty EnvironmentData is valid: it means "no authored
    // environment", the same way an all-default Atmosphere means "no fog".

    struct EnvironmentData
    {
        // Global fog (kAssetTypeAtmosphere) — half of the view-frequency constants.
        wz::asset::AssetKey atmosphere{};
        // Ambient light (kAssetTypeAmbientLighting).
        wz::asset::AssetKey ambient_lighting{};
        // Image-based environment lighting (kAssetTypeEnvironmentMap / HDRI).
        wz::asset::AssetKey hdri_environment{};
        // Global directional (sun/moon) light (kAssetTypeDirectLight). Local
        // point/spot lights are NOT frame-global and stay per-node.
        wz::asset::AssetKey directional_light{};

        // Any subset is a legitimate environment, including none, so there is
        // nothing to reject: each key is either empty or a real upstream key the
        // DAG already validated. Present for parity with the other recipe data
        // structs and as the seam a future invariant would live at.
        bool valid() const noexcept { return true; }
    };


    // ─── EnvironmentTable ────────────────────────────────────────────────────────
    //
    // Runtime owner of resolved environments. The AssetSystem stores only a
    // ResourceHandle; the actual EnvironmentData lives in this table.
    //
    // V1: append-only. Epoch starts at 1 — epoch 0 in a ResourceHandle always
    // indicates invalid. Follows the AtmosphereTable / PlacementTable shape.

    class EnvironmentTable
    {
    public:
        // Reserves slot 0 as the invalid sentinel so all real handles have id >= 1.
        EnvironmentTable();

        // Store a resolved environment and return a handle to it.
        wz::asset::ResourceHandle add(EnvironmentData environment);

        // Look up an environment by handle. Returns nullptr if the handle is stale
        // or out of range. Const — does not modify the table.
        const EnvironmentData* get(wz::asset::ResourceHandle handle) const;

        // Release all slots. Invalidates all outstanding handles.
        void destroy();

    private:
        struct Slot
        {
            uint32_t        epoch = 0;
            bool            occupied = false;
            EnvironmentData environment;
        };

        std::vector<Slot> slots_;
    };
}
