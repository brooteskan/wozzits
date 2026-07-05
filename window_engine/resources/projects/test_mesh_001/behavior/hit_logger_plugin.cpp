#include <engine/behavior/behavior_module_api.h>

#include "collidable.h"

// hit_logger -- on THIS node's hitbox, tallies damage taken. On COLLISION_ENTER it
// reads the striker's "collidable" state off `other` (kind / owner / damage), adds
// the damage to a per-tank running total held in this behavior's instance state
// (one hitbox per tank -> this total IS the tank's total damage taken), and logs the
// hit with attribution. Config "label" names the owner in the log.

namespace
{
    struct HitState
    {
        float total_damage = 0.0f;  // cumulative damage this tank has taken
    };

    // Both channels: a trigger collider fires COLLISION events; subscribing to
    // proximity too is harmless (this node has no proximity component).
    static const char* kHitEvents[] = { "collision.*", "proximity.*" };

    void hit_init(const WzBehaviorInitFacts* facts, WzBehaviorEntityId, void*)
    {
        if (auto* s = wz_instance_state<HitState>(facts)) {
            s->total_damage = 0.0f;
        }
    }

    void hit_on_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event) {
            return;
        }
        const WzBehaviorEventKind kind = wz_event_kind(event);
        if (kind != WZ_EVENT_COLLISION_ENTER
            && kind != WZ_EVENT_PROXIMITY_ENTER) {
            return;  // ENTER only -- ignore STAY/EXIT
        }

        char label[32] = "tank";
        uint32_t required = 0;
        wz_config_string(facts, "label", label, sizeof(label), &required);

        auto* s = wz_instance_state<HitState>(facts);
        const WzBehaviorEntityId other = wz_other(event);

        // The striker carries a "collidable" -- who owns it + how much it hurts.
        if (auto* c = wz_instance_state_of<collidable::State>(
                facts, other, collidable::kModule)) {
            const float total =
                s ? (s->total_damage += c->damage) : c->damage;
            wz_log_infof(
                facts, "[HIT] %s took %.1f dmg from entity %u -- total %.1f",
                label, (double)c->damage, (unsigned)c->owner, (double)total);
        } else {
            // No collidable on the striker: log the bare contact.
            wz_log_infof(
                facts, "[HIT] %s struck by entity %u (no collidable)",
                label, (unsigned)other);
        }
    }
}

WZ_BEHAVIOR_MODULE_INIT("hit_logger", hit_init, hit_on_event, kHitEvents)
