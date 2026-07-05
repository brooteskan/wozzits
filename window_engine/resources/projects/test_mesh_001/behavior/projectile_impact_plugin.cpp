#include <engine/behavior/behavior_module_api.h>

#include "projectile_impact.h"

// projectile_impact -- the flying projectile's OWN collider handler. On
// COLLISION_ENTER it records a hit + where it struck, in its instance state. The
// shooter (cannon_fire) polls that via wz_instance_state_of to freeze + burst the
// projectile and clears it at each launch. Put this on the "projectile" node,
// alongside its layer-4 collider. Stateful-but-tiny: see projectile_impact.h.

namespace
{
    // Trigger colliders fire COLLISION events; the projectile has no proximity
    // component so we only subscribe to collision.
    static const char* kEvents[] = { "collision.*" };

    void impact_init(
        const WzBehaviorInitFacts* facts, WzBehaviorEntityId, void*)
    {
        // Allocate + zero the block up front so the shooter can poll/clear it from
        // the very first frame (before any collision has occurred).
        if (auto* s = wz_instance_state<projectile_impact::State>(facts)) {
            s->hit = 0;
        }
    }

    void impact_on_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event) {
            return;
        }
        if (wz_event_kind(event) != WZ_EVENT_COLLISION_ENTER) {
            return;  // ENTER only -- one strike per pass-through
        }
        auto* s = wz_instance_state<projectile_impact::State>(facts);
        if (!s) {
            return;
        }
        // Record where the projectile is right now (event->entity == self); the
        // shooter bursts there. Falls back to the last position if the read fails.
        WzMat4 m{};
        if (wz_read_world_transform(facts, event->entity, &m)) {
            s->hx = m.m[12];
            s->hy = m.m[13];
            s->hz = m.m[14];
        }
        s->hit = 1;
    }
}

WZ_BEHAVIOR_MODULE_INIT(
    "projectile_impact", impact_init, impact_on_event, kEvents)
