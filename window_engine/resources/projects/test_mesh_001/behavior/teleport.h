#pragma once
// behavior/teleport.h
//
// The enemy tank "blink": a translucent cyan energy bubble expands around the
// tank, the tank vanishes and jumps to a new spot on the landscape, and the
// bubble shrinks to reveal it there. Header-only and driven from the agent like
// cannon_fire.h -- teleport::init() once, teleport::tick() every frame; a
// built-in timer starts a blink for now (Seam 2), and later the AI can call
// teleport::trigger() directly.
//
// Node structure it drives (all resolved by name once in init):
//   * self   -- the tank ROOT. Moving it carries the whole tank AND the bubble.
//   * body   -- the GLB tank-mesh subtree ("body", a descendant of the root).
//               Visibility is HIERARCHICAL, so hiding 'body' hides the whole tank
//               mesh (body -> turret -> gun) while the bubble -- an authored
//               SIBLING of body under the root -- stays visible. We must NOT hide
//               the root: that would hide the bubble too.
//   * bubble -- the "teleport_bubble" child (the cyan sphere renderable); its
//               local scale is driven 0 -> full -> 0 for the expand / shrink.
//   * terrain-- the landscape, sampled for the arrival height.
//
// Trivially-copyable POD, so it rides the behavior instance block through
// hot-reload + rebuild like the rest of the tank's state.

#include <engine/behavior/behavior_module_api.h>

#include "tank_drive.h"   // clampf

#include <math.h>

namespace teleport
{
    // ---- Tunables ----
    inline constexpr float  kExpandSeconds    = 0.30f; // bubble grows 0 -> full
    inline constexpr float  kShrinkSeconds    = 0.40f; // bubble collapses full -> 0
    inline constexpr float  kBubbleScale      = 6.0f;  // full radius (sphere mesh r=1)
    inline constexpr float  kTeleportDist     = 45.0f; // jump distance (world units)
    inline constexpr float  kRideHeight       = 0.5f;  // lift above the sampled ground
    inline constexpr float  kRevealFraction   = 0.45f; // reveal the tank once shrink is this far

    inline constexpr const char* kBubbleName = "teleport_bubble";
    inline constexpr const char* kBodyName   = "body";

    enum class Phase : uint8_t { Idle = 0, Expand, Shrink };

    struct State
    {
        WzBehaviorEntityId self    = WZ_INVALID_BEHAVIOR_ENTITY;  // tank root
        WzBehaviorEntityId bubble  = WZ_INVALID_BEHAVIOR_ENTITY;  // cyan sphere child
        WzBehaviorEntityId body    = WZ_INVALID_BEHAVIOR_ENTITY;  // GLB mesh subtree
        WzBehaviorEntityId terrain = WZ_INVALID_BEHAVIOR_ENTITY;  // for arrival height
        Phase   phase = Phase::Idle;
        float   timer = 0.0f;       // seconds into the current phase
        float   dir   = 0.0f;       // accumulating blink heading (rad)
        float   dx = 0.0f, dy = 0.0f, dz = 0.0f;  // chosen arrival point (world)
    };

    inline void set_bubble_scale(
        const WzBehaviorFrameFacts* facts, const State* s, float scale)
    {
        if (s->bubble != WZ_INVALID_BEHAVIOR_ENTITY) {
            wz_write_set_local_scale(facts, s->bubble, scale, scale, scale);
        }
    }

    inline void show_bubble(
        const WzBehaviorFrameFacts* facts, const State* s, uint8_t vis)
    {
        if (s->bubble != WZ_INVALID_BEHAVIOR_ENTITY) {
            wz_write_set_visible(facts, s->bubble, vis);
        }
    }

    inline void show_body(
        const WzBehaviorFrameFacts* facts, const State* s, uint8_t vis)
    {
        if (s->body != WZ_INVALID_BEHAVIOR_ENTITY) {
            wz_write_set_visible(facts, s->body, vis);
        }
    }

    inline void init(
        const WzBehaviorInitFacts* facts,
        WzBehaviorEntityId self,
        WzBehaviorEntityId terrain,
        State* s)
    {
        // Re-bind the node handles (runtime ids renumber on every rebuild) but PRESERVE
        // the in-progress blink -- phase / timer / destination / fan angle. Like
        // tank_lifecycle (and unlike cannon_fire, whose brief flash is safe to reset),
        // this effect has a LASTING side effect: mid-blink the tank body is hidden for
        // ~0.5s, and that runtime visible=0 flag survives a rebuild. If init reset the
        // phase to Idle, the Shrink reveal would never run and a live tank would drive +
        // fire INVISIBLE until its next redeploy. Preserving the phase lets tick() finish
        // the blink (reveal the body) straight across the rebuild. First alloc
        // zero-constructs State (phase = Idle), so nothing needs an explicit reset.
        //
        // The DIED-mid-blink case (a parked tank that redeploys) is handled separately by
        // the self.activated reset: a parked tank doesn't tick, so it must be forced back
        // to a clean visible state on deploy rather than left to finish the blink here.
        s->self = self;
        s->terrain = terrain;
        s->bubble = WZ_INVALID_BEHAVIOR_ENTITY;   // reset before re-find so a failed
        s->body = WZ_INVALID_BEHAVIOR_ENTITY;     // lookup leaves INVALID, not a stale id
        wz_find_descendant_by_name(facts, self, kBubbleName, &s->bubble);
        wz_find_descendant_by_name(facts, self, kBodyName, &s->body);
        wz_log_infof(
            facts, "[teleport] bubble=%u body=%u phase=%u",
            (unsigned)(s->bubble != WZ_INVALID_BEHAVIOR_ENTITY),
            (unsigned)(s->body != WZ_INVALID_BEHAVIOR_ENTITY),
            (unsigned)s->phase);
    }

    inline bool is_blinking(const State* s)
    {
        return s && s->phase != Phase::Idle;
    }

    // Begin a blink: pick a landing spot on the landscape and start the expand.
    // No-op if already blinking.
    inline void trigger(const WzBehaviorFrameFacts* facts, State* s)
    {
        if (s->phase != Phase::Idle
            || s->self == WZ_INVALID_BEHAVIOR_ENTITY)
        {
            return;
        }
        WzMat4 w{};
        if (!wz_read_world_transform(facts, s->self, &w)) {
            return;
        }
        // Fan successive blinks around the tank (golden angle) so it doesn't keep
        // jumping the same way.
        s->dir += 2.39996323f;
        const float nx = cosf(s->dir);
        const float nz = sinf(s->dir);
        s->dx = w.m[12] + nx * kTeleportDist;
        s->dz = w.m[14] + nz * kTeleportDist;
        s->dy = w.m[13];   // fallback: keep the current height

        // Snap to the landscape: sample the terrain height at the arrival XZ.
        if (s->terrain != WZ_INVALID_BEHAVIOR_ENTITY) {
            WzSurfaceSample surf{};
            if (wz_sample_terrain_surface(facts, s->terrain, s->dx, s->dz, &surf)
                && surf.hit)
            {
                s->dy = surf.position.y + kRideHeight;
            }
        }

        s->timer = 0.0f;
        s->phase = Phase::Expand;
        set_bubble_scale(facts, s, 0.0f);
        show_bubble(facts, s, 1u);
    }

    // Advance an in-progress blink. Idle until the agent calls trigger() (wired to
    // the tank's BLINK decision qubit).
    inline void tick(const WzBehaviorFrameFacts* facts, State* s)
    {
        if (!facts || !s || s->phase == Phase::Idle) {
            return;
        }

        s->timer += wz_delta_seconds(facts);

        if (s->phase == Phase::Expand) {
            const float t =
                tank_drive::clampf(s->timer / kExpandSeconds, 0.0f, 1.0f);
            set_bubble_scale(facts, s, t * kBubbleScale);
            if (t >= 1.0f) {
                // Enveloped: hide the tank mesh and jump the root (the bubble
                // rides along) to the arrival point, then collapse the bubble
                // there.
                show_body(facts, s, 0u);
                wz_write_set_world_translation(
                    facts, s->self, s->dx, s->dy, s->dz);
                s->timer = 0.0f;
                s->phase = Phase::Shrink;
            }
            return;
        }

        // Shrink at the arrival point; reveal the tank partway through so it
        // "materialises" as the shell collapses.
        const float t =
            tank_drive::clampf(s->timer / kShrinkSeconds, 0.0f, 1.0f);
        set_bubble_scale(facts, s, (1.0f - t) * kBubbleScale);
        if (t >= kRevealFraction) {
            show_body(facts, s, 1u);
        }
        if (t >= 1.0f) {
            show_body(facts, s, 1u);
            set_bubble_scale(facts, s, 0.0f);
            show_bubble(facts, s, 0u);
            s->phase = Phase::Idle;
        }
    }
}
