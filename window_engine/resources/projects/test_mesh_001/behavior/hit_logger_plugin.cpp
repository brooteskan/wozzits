#include <engine/behavior/behavior_module_api.h>

// hit_logger -- logs when THIS node's collider is hit (COLLISION/PROXIMITY ENTER).
// Put it on a hitbox node. Config "label" names the owner in the log (e.g.
// "player", "enemy"). This is the first cut of the collision hit loop -- it just
// logs the raw collision + the other entity's handle. The NEXT step reads the
// other entity's data off that handle via wz_instance_state_of (the Collidable
// convention): who fired it, how much damage, etc.

namespace
{
    // Both channels: a trigger collider fires COLLISION events; subscribing to
    // proximity too is harmless (this node has no proximity component) and future-
    // proofs a proximity-sensor variant.
    static const char* kHitEvents[] = { "collision.*", "proximity.*" };

    void hit_init(const WzBehaviorInitFacts*, WzBehaviorEntityId, void*)
    {
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
            return;  // ENTER only -- ignore STAY/EXIT for the log
        }

        char label[32] = "tank";
        uint32_t required = 0;
        wz_config_string(facts, "label", label, sizeof(label), &required);

        wz_log_infof(
            facts, "[HIT] %s collider struck by entity %u",
            label, (unsigned)wz_other(event));
    }
}

WZ_BEHAVIOR_MODULE_INIT("hit_logger", hit_init, hit_on_event, kHitEvents)
