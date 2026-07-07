#pragma once
// behavior/agent_tank.h

#include <engine/behavior/behavior_module_api.h>
#include "tank_drive.h"
#include "cannon_fire.h"
#include "tank_lifecycle.h"

// Enemy gun elevation travel limits (radians, relative to the gun's LEVEL rest
// pose), the AI counterpart to the player's in player_tank.h. The agent pitches
// the gun between these to hold a target above/below it. min < 0 lets the muzzle
// depress below horizontal to aim DOWN at a lower target (reduce if it clips the
// hull).
inline constexpr float kGunElevationMax = 2 * 0.2617994f;  // 30 deg above horizontal
inline constexpr float kGunElevationMin = -0.2617994f;     // 15 deg depression

// Total damage an enemy tank absorbs before it is destroyed. The player's shell
// deals 25 (its projectile's collidable), so this is ~4 clean hits. On death the
// enemy respawns at the squad command node (its HQ). Tune to taste.
inline constexpr float kEnemyMaxHealth = 100.0f;

// Shared squad roster (behavior SHARED state, key "squad"): tanks register on
// spawn to claim a slot in the commander's group agent, and the commander reads
// the count to size that agent. Lives in shared state so it survives rebuilds and
// is visible to both the tank and commander plugins. (Grow-only for now -- despawn
// isn't wired, so it never shrinks yet.)
struct SquadRoster {
    int member_count = 0;

    // DOCTRINE-LEARNING squad tally (grow-only counters; the commander rewards on
    // the DELTA between re-anneals): times a squad member acquired a shot on the
    // player vs. times a member came under the player's fire. Their running
    // difference is the squad's net exchange -- the signal the commander's doctrine
    // memory learns from ("is pressing paying off against THIS player?").
    int shots_landed = 0;
    int fire_taken = 0;
};
inline constexpr const char* kSquadRosterKey = "squad";

struct QuantumTankState {

    float heading = 0.0f;
    float speed = 0.0f;
    tank_drive::Chassis chassis;  // turret handle (chassis.turret) + turret aim
    cannon_fire::State cannon;    // the shared "fire the cannon" sequence state

    // The gun (barrel) node -- the "gun" child of the turret in the tank GLB
    // (body -> turret -> gun); the muzzle-flash anchor.
    WzBehaviorEntityId barrel = WZ_INVALID_BEHAVIOR_ENTITY;

    uint32_t ammo = 1000;  // effectively unlimited; enemy firing isn't ammo-gated

    // The clipmap landscape node (its Heightfield Collision component is what we
    // sample -- for line of sight now, and ground height later). Same node for both.
    WzBehaviorEntityId terrain = WZ_INVALID_BEHAVIOR_ENTITY;
    WzBehaviorEntityId canon_audio = WZ_INVALID_BEHAVIOR_ENTITY;
    WzBehaviorEntityId player = WZ_INVALID_BEHAVIOR_ENTITY;

    WzBehaviorEntityId target = WZ_INVALID_BEHAVIOR_ENTITY;

    // The hidden "command" node whose quantum_agent decides the squad's group
    // order (PRESS/HARASS). A tank READS it and folds it into its own goals; the
    // commander behavior itself leaves this invalid (it IS the command node).
    WzBehaviorEntityId command = WZ_INVALID_BEHAVIOR_ENTITY;

    // Life/death: the "hitbox" child's hit_logger tally feeds the lifecycle
    // machine (Alive/Dead), which respawns the enemy at the command node on death.
    WzBehaviorEntityId hitbox = WZ_INVALID_BEHAVIOR_ENTITY;
    tank_lifecycle::State lifecycle;

    // --- World snapshot: sensed every frame; the raw inputs the cognition goals
    // read. Grow this as the tank learns to care about more of the world. ---
    float distance_to_target = 0.0f;  // world units to the target
    float bearing_to_target = 0.0f;   // signed nose->target angle (rad); 0 = ahead
    float closing_rate = 0.0f;        // world u/s: >0 we're closing, <0 opening
    float target_speed = 0.0f;        // the target's world speed (u/s)
    float target_heading = 0.0f;      // the target's heading (rad)
    float ground_height = 0.0f;       // terrain height under us (TODO: sense it)

    // Sense-pass bookkeeping (velocity = delta-position / delta-time).
    float target_prev_x = 0.0f;
    float target_prev_z = 0.0f;
    float prev_distance = 0.0f;
    double last_sense_time = -1.0;    // sim_time of last sense (<0 = none yet)
    double next_reanneal_time = 0.0;  // sim_time to re-deliberate the stance
    double last_reanneal_time = 0.0;  // sim_time of the last re-anneal (meta cadence)

    // How far off the turret's gun is from the target (rad); 0 = on target.
    float aim_error = 0.0f;

    // Current CONTEXT bucket for contextual learning (recomputed each frame):
    // 1 = the target's hull is pointed at us (it is ENGAGING/braced), 0 = it is
    // not (fleeing/passive). Both the reward (which (context,action) branch to
    // reinforce) and the conditional read (which context to act on) key off this,
    // so they must agree within a frame.
    uint8_t context_engaging = 0;

    // Edge-tracking for the learning logs: last frame's "gun on target in range"
    // (had_shot) and "player's hull on us in range" (under_fire), so we log only on
    // the RISING edge -- acquiring a shot / starting to take fire -- not every frame.
    uint8_t had_shot = 0;
    uint8_t under_fire = 0;

    // Observation-forced decoherence: whether the player is currently looking at us
    // (last frame), so we log only when it flips.
    uint8_t observed = 0;

    // Line of sight to the player last frame (1 = clear); starts clear so the first
    // occlusion logs a LOST edge.
    uint8_t has_los = 1;

    // Last committed FIRE disposition (-2 never read, -1 deliberating, 0 weapons-
    // free, 1 conserve), so we log only when it flips.
    int8_t fire_stance = -2;

    // Cannon reload gate: sim_time the tank may next fire (fires whenever it has a
    // shot lined up, at most once per kFireCooldown). Unlimited ammo.
    double next_fire_time = 0.0;

    // Last committed disposition of the co-located quantum_agent (if any):
    // -2 = never read, -1 = deliberating, 0/1 = the chosen outcome. We react
    // only on a CHANGE, so the announcement fires once per collapse.
    int8_t last_decision = -2;

    float drive_speed = 6.0f; // remove soon

    // Small stable per-instance id / squad slot, claimed once from the shared
    // roster on first init ("[qtank:0]", "[qtank:1]"). -1 = unassigned (preserved
    // across rebuilds).
    int tank_id = -1;

    // Commander-only: the squad size it last reshaped its group agent to (-1 =
    // never), so it reshapes only when the roster count changes.
    int squad_size = -1;

    // Commander-only doctrine learning: the squad tallies last seen, so the
    // commander rewards its doctrine memory on the DELTA each re-anneal.
    int prev_shots_landed = 0;
    int prev_fire_taken = 0;

    // Commander-only: sim_time the commander may next bring up a reinforcement
    // (spawn cooldown), so populating the squad from HQ is paced -- a spawn is
    // O(scene). Preserved across rebuilds (commander_init never resets it), so the
    // cadence survives the rebuild each spawn triggers.
    double next_spawn_time = 0.0;
};

// Populate the world snapshot the cognition goals read: distance + bearing to the
// target, the target's velocity (tracked across frames) and the closing rate.
// SHARED by the tank actuator and the commander so the sensing is defined once.
// (The commander's own node position is arbitrary, so it reads target_speed --
// absolute and meaningful -- rather than distance.)
inline void sense_world(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    QuantumTankState* state)
{
    WzMat4 self_world{};
    WzVec3 tpos{};
    if (!wz_self_world_transform(facts, event, &self_world)
        || !wz_read_world_position(facts, state->target, &tpos))
    {
        return;
    }
    const float dx = tpos.x - self_world.m[12];
    const float dz = tpos.z - self_world.m[14];
    const float dist = sqrtf(dx * dx + dz * dz);

    state->bearing_to_target =
        tank_drive::face_bearing_to(facts, event, state->target);

    const double now = wz_sim_time(facts);
    if (state->last_sense_time >= 0.0) {
        const float dt = static_cast<float>(now - state->last_sense_time);
        if (dt > 1e-4f) {
            const float vx = (tpos.x - state->target_prev_x) / dt;
            const float vz = (tpos.z - state->target_prev_z) / dt;
            state->target_speed = sqrtf(vx * vx + vz * vz);
            if (state->target_speed > 0.05f) {
                state->target_heading = atan2f(-vz, vx);
            }
            state->closing_rate = (state->prev_distance - dist) / dt;
        }
    }

    state->distance_to_target = dist;
    state->target_prev_x = tpos.x;
    state->target_prev_z = tpos.z;
    state->prev_distance = dist;
    state->last_sense_time = now;

    // TODO: sample the terrain under us (state->terrain) into ground_height +
    // a slope, then let it bias the goals (avoid steep climbs, prefer cover).
}