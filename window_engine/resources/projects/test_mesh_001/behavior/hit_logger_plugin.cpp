#include <engine/behavior/behavior_module_api.h>

#include "collidable.h"
#include "tank_damage.h"

// hit_logger -- on THIS node's hitbox, tallies damage taken. On COLLISION_ENTER it
// reads the striker's "collidable" state off `other` (kind / owner / damage), adds
// the damage to a per-tank running total held in this behavior's instance state
// (one hitbox per tank -> this total IS the tank's total damage taken), and logs the
// hit with attribution. Config "label" names the owner in the log.

namespace
{
    // Both channels: a trigger collider fires COLLISION events; subscribing to
    // proximity too is harmless (this node has no proximity component).
    static const char* kHitEvents[] = { "collision.*", "proximity.*" };

    void hit_init(const WzBehaviorInitFacts* facts, WzBehaviorEntityId, void*)
    {
        // Ensure the tally block exists (zeroed on FIRST alloc, then PRESERVED as-is
        // across rebuilds), but do NOT reset it here. init re-runs on every structural
        // rebuild, so zeroing would heal a live tank as a side effect of, e.g., a
        // prewarm spawn -- silently wiping accumulated damage mid-combat. A tank is
        // (re)started healthy at its real lifecycle points instead: the enemy on deploy
        // (self.activated) and both tanks on respawn (tank_lifecycle), not on re-init.
        (void)wz_instance_state<tank_damage::Tally>(facts);
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

        auto* s = wz_instance_state<tank_damage::Tally>(facts);
        const WzBehaviorEntityId other = wz_other(event);

        // The striker carries a "collidable" -- who owns it + how much it hurts.
        if (auto* c = wz_instance_state_of<collidable::State>(
                facts, other, collidable::kModule)) {
            const float total =
                s ? (s->total += c->damage) : c->damage;
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
