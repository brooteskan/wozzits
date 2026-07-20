#include <engine/behavior/behavior_module_api.h>

#include "tank_drive.h"   // enemy_tank_v1's own drive core (sibling header)
#include "tank_state.h"

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

    // Slot defaults. These have to live HERE, in C++ � a declared param's default is
    // the inspector's, not ours (see the callout). Same shape as the spawner's
    // `char prefab[64] = "enemy_tank_v1"` in Chapter 2.
    const char* const kTargetDefaults[TankState::kMaxTargets] = { "tank", "", "", "" };

    // Fuel burned per second at `throttle` (0..1). QUADRATIC in throttle, so the last
    // slice of speed is the dearest; the idle term is what stops crawling from being
    // free. Together they put a minimum of fuel-per-METRE somewhere in the middle.
    float burn_per_second(const TankState* s, float throttle) {
        return s->idle_burn
            + (s->full_burn - s->idle_burn) * throttle * throttle;
    }

    void acquire_targets(const WzBehaviorFrameFacts* facts, TankState* s) {
        // Which node is the pump? Resolved once, compared by HANDLE below � so a slot
        // pointing at it is recognised however the name was spelled.
     // Which node is the pump? Resolved once, compared by HANDLE in the loop � so a
    // slot pointing at it is recognised however the name was spelled.
        char depot_name[48] = "";
        uint32_t depot_required = 0;
        wz_config_string(
            facts, "depot", depot_name, sizeof(depot_name), &depot_required);
        WzBehaviorEntityId depot_entity = WZ_INVALID_BEHAVIOR_ENTITY;
        if (depot_name[0] != '\0') {
            (void)wz_find_entity_by_name(facts, depot_name, &depot_entity);
        }

        s->target_count = 0;
        s->depot = TankState::kNoDepot;
        for (uint8_t i = 0; i < TankState::kMaxTargets; ++i) {
            char key[16];
            snprintf(key, sizeof(key), "target_%u", (unsigned)i);

            char name[48] = "";
            uint32_t required = 0;
            wz_config_string(facts, key, name, sizeof(name), &required);
            if (name[0] == '\0') continue;            // slot genuinely blank

            WzBehaviorEntityId e = WZ_INVALID_BEHAVIOR_ENTITY;
            if (wz_find_entity_by_name(facts, name, &e)
                && e != WZ_INVALID_BEHAVIOR_ENTITY)
            {
                // NEW � targets are PACKED as they resolve, so the authored slot number
                // is NOT the runtime one: remember where the pump actually landed. `e` is
                // valid in this branch, so the compare is false whenever the pump didn't
                // resolve � depot_entity stays INVALID, which can't equal a valid handle.
                const bool is_depot = (e == depot_entity);
                if (is_depot) {
                    s->depot = s->target_count;
                }
                s->targets[s->target_count++] = e;
                wz_log_infof(facts, "[enemy_tank_v1] target %u = '%s'%s",
                    (unsigned)(s->target_count - 1), name, is_depot ? " (depot)" : "");
            }
            else {
                wz_log_infof(
                    facts, "[enemy_tank_v1] target '%s' not found (skipped)", name);
            }
        }

        // Config, so: read once, here. Each lands the kParams default unless authored.
        (void)wz_config_float(facts, "fuel_capacity", &s->capacity);
        (void)wz_config_float(facts, "cruise_throttle", &s->throttle);
        (void)wz_config_float(facts, "idle_burn", &s->idle_burn);
        (void)wz_config_float(facts, "full_burn", &s->full_burn);
        (void)wz_config_float(facts, "refuel_radius", &s->refuel_radius);
        (void)wz_config_float(facts, "refuel_rate", &s->refuel_rate);

        s->fuel = s->capacity;   // roll out full
        s->stranded = false;
        wz_log_infof(facts,
            "[enemy_tank_v1] acquired %u target(s), fuel %.0f, throttle %.2f "
            "(%.2f fuel/s)",
            (unsigned)s->target_count, (double)s->fuel, (double)s->throttle,
            (double)burn_per_second(s, s->throttle));
    }

    void tank_on_event(const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event, void*) {
        if (!facts || !event) return;
        TankState* s = wz_instance_state<TankState>(facts);
        if (!s) return;

        if (wz_event_kind(event) == WZ_EVENT_SELF_START) {
            acquire_targets(facts, s);   // Chapter 3's call � only the name changed
            return;
        }
        if (wz_event_kind(event) != WZ_EVENT_FRAME_UPDATE || s->target_count == 0) return;

        // ��� everything below replaces Chapter 3's "drive at the chosen target" ���

        const float dt = wz_delta_seconds(facts);
        const float throttle = tank_drive::clampf(s->throttle, 0.0f, 1.0f);

        // REFUEL � physical, not a decision. You fill up when you are AT the pump,
        // whether or not going there was the plan. Deciding to GO is the mind's job.
        if (s->depot < s->target_count) {
            float distance = 0.0f;
            if (wz_distance_between_world_positions(
                facts, wz_self(event), s->targets[s->depot], &distance)
                && distance <= s->refuel_radius)
            {
                s->fuel += s->refuel_rate * dt;
                if (s->fuel > s->capacity) s->fuel = s->capacity;
                if (s->stranded && s->fuel > 0.0f) {
                    s->stranded = false;   // stranded ON the pump: it saves itself
                    wz_log_info(facts, "[enemy_tank_v1] refuelled");
                }
            }
        }

        // BURN � always, even parked: an idling engine still drinks.
        s->fuel -= burn_per_second(s, throttle) * dt;
        if (s->fuel <= 0.0f) {
            if (!s->stranded) {
                s->stranded = true;
                tank_drive::drive_facing(facts, event, 0.0f, 0.0f);   // stop
                wz_log_info(facts, "[enemy_tank_v1] out of fuel -- despawning");
                // DIE: remove the whole tank so the presence-gated spawner replaces us.
                wz_remove_node(facts, wz_self(event));
            }
            return;
        }

        // PUBLISH � the seam. After the burn, so the number is this frame's truth.
        // FULLNESS (0..1), not raw litres -- so the mind's fuel sense means the same
        // thing at any capacity: half a tank is 0.5 whether it holds 100 or 300. The
        // chart decides on this fraction, so the turn-around scales with the tank.
        const double fullness =
            s->capacity > 0.0f ? (double)s->fuel / (double)s->capacity : 0.0;
        wz_set_self_scalar(facts, event, "fuel", fullness);

        // PROGRESS -- ground made up toward the player on the leg that just ENDED.
        // A chart cannot work this out: pure ops are stateless, and a delta needs a
        // memory of where the leg began. So the body senses it, exactly as it senses
        // fuel. A "leg" is one committed decision -- it ends when the target changes.
        // (targets[0] is the player by the same packing luxury the chart relies on.)
        float player_range = 0.0f;
        if (s->target_count > 0
            && wz_distance_between_world_positions(
                facts, wz_self(event), s->targets[0], &player_range))
        {
            if (s->last_target == TankState::kNoLeg) {
                s->leg_start_range = player_range;   // first leg of this life
                s->last_target = s->target_chosen;
            }
            else if (s->target_chosen != s->last_target) {
                s->last_leg_closed = s->leg_start_range - player_range;
                s->leg_start_range = player_range;
                s->last_target = s->target_chosen;
            }
        }
        wz_set_self_scalar(facts, event, "closed", (double)s->last_leg_closed);
        wz_set_self_scalar(facts, event, "target", (double)s->target_chosen);
        // What it ACTUALLY drove at. The chart re-deliberates every few seconds, so by
        // the time a trip pays out the committed gait is no longer the one the foray
        // ran on -- the reward has to credit the gait that was really used.
        wz_set_self_scalar(facts, event, "throttle", (double)s->throttle);

        // DRIVE � only with fuel to burn, and only as fast as the throttle says.
        if (s->fuel <= 0.0f) {
            if (!s->stranded) {
                s->stranded = true;
                tank_drive::drive_facing(facts, event, 0.0f, 0.0f);
                wz_log_info(facts, "[enemy_tank_v1] out of fuel");
            }
            return;
        }

        const WzBehaviorEntityId target =
            s->targets[s->target_chosen < s->target_count ? s->target_chosen : 0];
        const float yaw = tank_drive::face_yaw_rate(
            facts, event, target, kTurnGain, tank_drive::kTurnSpeed);
        tank_drive::drive_facing(
            facts, event, yaw, tank_drive::kMoveSpeed * throttle);
    }

    const char* kChannels[] = { "self.start", "frame.update" };

    // Declared params -> four labeled text fields in the inspector.
    const WzBehaviorParamDesc kParams[] = {
        // Target NAMES are your scene's business, so they ship blank (Chapter 3).
        { "target_0", "Target 0", WZ_BEHAVIOR_PARAM_STRING, 0.0, "" },
        { "target_1", "Target 1", WZ_BEHAVIOR_PARAM_STRING, 0.0, "" },
        { "target_2", "Target 2", WZ_BEHAVIOR_PARAM_STRING, 0.0, "" },
        { "target_3", "Target 3", WZ_BEHAVIOR_PARAM_STRING, 0.0, "" },
        // The pump's name is one WE tell you to create, so it ships a real default.
        { "depot",           "Depot target (node name)",  WZ_BEHAVIOR_PARAM_STRING, 0.0,   "depot" },
        { "fuel_capacity",   "Fuel capacity",             WZ_BEHAVIOR_PARAM_FLOAT,  100.0, "" },
        { "cruise_throttle", "Throttle 0-1 (hard-wired for now)",
                                                           WZ_BEHAVIOR_PARAM_FLOAT,  1.0,   "" },
        { "idle_burn",       "Fuel/sec parked",           WZ_BEHAVIOR_PARAM_FLOAT,  0.5,   "" },
        { "full_burn",       "Fuel/sec flat out",         WZ_BEHAVIOR_PARAM_FLOAT,  6.0,   "" },
        { "refuel_radius",   "Refuel radius",             WZ_BEHAVIOR_PARAM_FLOAT,  8.0,   "" },
        { "refuel_rate",     "Fuel/sec at depot",         WZ_BEHAVIOR_PARAM_FLOAT,  25.0,  "" },
    };
}

WZ_BEHAVIOR_MODULE_INIT_PARAMS(
    "enemy_tank_v1", tank_init, tank_on_event, kChannels, kParams)
