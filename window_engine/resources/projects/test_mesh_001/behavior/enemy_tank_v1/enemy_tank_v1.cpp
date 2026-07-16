#include <engine/behavior/behavior_module_api.h>

#include "tank_drive.h"   // enemy_tank_v1's own drive core (sibling header)

// enemy_tank_v1 -- a fresh enemy tank, built up from scratch, one simple step at a time.
//
// STEP 1 (this file): just a BODY that drives toward the player. No cognition yet -- no
// quantum mind, no statechart, no firing. The point is to prove the chassis moves under
// its own behavior before we hand the steering wheel to a mind. It finds the player node
// once (by its unique name "tank"), then every frame turns its nose toward the player and
// drives forward. That's the whole tank for now.
//
// Where this is going: later steps demote this plugin to a thin "body" -- it will SENSE
// the world and publish named scalars (range, damage, ...) and PROVIDE actuators (drive,
// fire), while a statechart reading those scalars drives an editable quantum mind that
// decides what the tank does. This file is the seed of that body.

namespace
{
    // All the state this step needs: who we're chasing. Trivially copyable, so it rides
    // the instance-state block across hot-reloads like every other behavior's state.
    struct TankState
    {
        WzBehaviorEntityId target = WZ_INVALID_BEHAVIOR_ENTITY;
    };

    // Steering + drive tuning for the chase.
    constexpr float kTurnGain = 3.0f;                 // bearing error (rad) -> yaw rate
    constexpr float kChaseSpeed = tank_drive::kMoveSpeed;   // full throttle toward the player

    void tank_init(const WzBehaviorInitFacts* facts, WzBehaviorEntityId, void*)
    {
        // Ensure the state block exists (zeroed on first alloc, preserved across
        // rebuilds). The target is resolved lazily on the first frame, where the
        // by-name lookup is available.
        (void)wz_instance_state<TankState>(facts);
    }

    void tank_on_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event || wz_event_kind(event) != WZ_EVENT_FRAME_UPDATE) {
            return;
        }
        TankState* s = wz_instance_state<TankState>(facts);
        if (!s) {
            return;
        }

        // Resolve the player ONCE, by its unique node name "tank" (robust to the scene
        // rebuilding / the player respawning -- an id would go stale, a name won't).
        if (s->target == WZ_INVALID_BEHAVIOR_ENTITY) {
            wz_find_entity_by_name(facts, "tank", &s->target);
            if (s->target == WZ_INVALID_BEHAVIOR_ENTITY) {
                return;   // player not in the scene yet -- try again next frame
            }
        }

        // Turn the nose toward the player and drive forward. face_yaw_rate returns a yaw
        // rate (rad/s) already clamped to the turn limit; drive_facing sends us along the
        // hull's true forward at that yaw. One line of "AI": chase.
        const float yaw = tank_drive::face_yaw_rate(
            facts, event, s->target, kTurnGain, tank_drive::kTurnSpeed);
        tank_drive::drive_facing(facts, event, yaw, kChaseSpeed);
    }

    const char* kChannels[] = { "frame.update" };
}

WZ_BEHAVIOR_MODULE_INIT("enemy_tank_v1", tank_init, tank_on_event, kChannels)
