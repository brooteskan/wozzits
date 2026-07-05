#include <engine/behavior/behavior_module_api.h>

#include "collidable.h"

// collidable -- identity + last-collision record for anything that can be struck
// (generalizes the old projectile_impact). init reads the authored kind/damage/team;
// on COLLISION_ENTER it records the strike (position + flag) for the owner to read.
// owner is stamped at runtime by whoever owns this collider -- e.g. cannon_fire
// stamps its projectile's shooter (the tank root). See collidable.h.

namespace
{
    static const char* kEvents[] = { "collision.*" };

    void collidable_init(
        const WzBehaviorInitFacts* facts, WzBehaviorEntityId, void*)
    {
        auto* s = wz_instance_state<collidable::State>(facts);
        if (!s) {
            return;
        }
        double kind = 0.0;
        wz_config_number(facts, "kind", &kind);
        s->kind = (uint32_t)kind;
        wz_config_float(facts, "damage", &s->damage);
        double team = 0.0;
        wz_config_number(facts, "team", &team);
        s->team = (int32_t)team;
        s->owner = WZ_INVALID_BEHAVIOR_ENTITY;  // stamped at runtime by the owner
        s->hit = 0;
    }

    void collidable_on_event(
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
        auto* s = wz_instance_state<collidable::State>(facts);
        if (!s) {
            return;
        }
        // Record where this collider is right now (event->entity == self) for the
        // owner to read; falls back to the last position if the read fails.
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
    "collidable", collidable_init, collidable_on_event, kEvents)
