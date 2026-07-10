#pragma once
// behavior/agent_tank.h

#include <engine/behavior/behavior_module_api.h>
#include "tank_drive.h"
#include "cannon_fire.h"
#include "tank_lifecycle.h"
#include "teleport.h"

// Enemy gun elevation travel limits (radians, relative to the gun's LEVEL rest
// pose), the AI counterpart to the player's in player_tank.h. The agent pitches
// the gun between these to hold a target above/below it. min < 0 lets the muzzle
// depress below horizontal to aim DOWN at a lower target (reduce if it clips the
// hull).
inline constexpr float kGunElevationMax = 2 * 0.2617994f;  // 30 deg above horizontal
inline constexpr float kGunElevationMin = 0.5 * -0.2617994f;     // 7.5 deg depression

// Total damage an enemy tank absorbs before it is destroyed. The player's shell
// deals 25 (its projectile's collidable), so this is ~4 clean hits. On death the
// enemy respawns at the squad command node (its HQ). Tune to taste.
inline constexpr float kEnemyMaxHealth = 100.0f;

// The group-agent member cap: the commander's quantum group agent has a hub qubit
// (0) plus ONE stance qubit per squad member (slot = tank_id + 1), bounded by the
// 4-decision agent cap -> at most this many coordinated members at once. A pool may
// prewarm MORE enemy_tank instances than this; the roster LEASE below keeps only
// this many in the squad's shared wave function, so pool_size is free to exceed it.
inline constexpr int kSquadLeaseSlots = 3;

// Shared squad roster (behavior SHARED state, key "squad"): a LEASE over the
// kSquadLeaseSlots group-agent member slots. A tank claims the lowest free slot when
// it DEPLOYS (unpark) and releases it when it dies (recycles back to the pool), so
// membership tracks the LIVE squad and never exceeds the group-agent cap regardless
// of how many instances the pool holds. Lives in shared state so it survives
// rebuilds and is visible to both the tank and commander plugins.
struct SquadRoster {
    uint8_t slot_taken[kSquadLeaseSlots] = { 0, 0, 0 };  // 1 = leased
    int     active_members = 0;   // == count of leased slots; the commander sizes to this

    // DOCTRINE-LEARNING squad tally (grow-only counters; the commander rewards on
    // the DELTA between re-anneals): times a squad member acquired a shot on the
    // player vs. times a member came under the player's fire. Their running
    // difference is the squad's net exchange -- the signal the commander's doctrine
    // memory learns from ("is pressing paying off against THIS player?").
    int shots_landed = 0;
    int fire_taken = 0;
};
inline constexpr const char* kSquadRosterKey = "squad";

// Claim the lowest free squad slot (0..kSquadLeaseSlots-1) -> the tank's group-agent
// member index (its qubit slot is index + 1). Returns -1 if the squad is full, in
// which case the tank fights SOLO (uncoordinated) -- the push-goals path already
// treats tank_id < 0 as "no group membership".
inline int squad_roster_claim(SquadRoster* r) {
    if (!r) {
        return -1;
    }
    for (int i = 0; i < kSquadLeaseSlots; ++i) {
        if (!r->slot_taken[i]) {
            r->slot_taken[i] = 1u;
            r->active_members++;
            return i;
        }
    }
    return -1;
}

// Release a leased slot back to the squad (on death/recycle). Idempotent.
inline void squad_roster_release(SquadRoster* r, int slot) {
    if (!r || slot < 0 || slot >= kSquadLeaseSlots || !r->slot_taken[slot]) {
        return;
    }
    r->slot_taken[slot] = 0u;
    r->active_members--;
}

// Commander-only ORDER re-anneal phase -- a plain-enum state machine (the teleport.h
// idiom) that orders the cognition-engine calls. The PRESS/HARASS order re-deliberates
// only on a MATERIAL change, not a fixed timer, so it LATCHES instead of re-rolling
// every window:
//   * Deliberating -- the group was just rearmed and is annealing toward a commit. We
//     only READ the decision here (waiting for it to settle); we never rearm.
//   * Holding      -- the order is latched. We act on it (feed reinforce deploys) and
//     watch for a trigger; the group is REARMED only on the transition back to
//     Deliberating.
// Because reads live in one state and the rearm lives on the transition, reading a
// decision in the same block it is rearmed -- the async-commit trap -- is structurally
// impossible.
enum class OrderPhase : uint8_t { Deliberating = 0, Holding };

struct QuantumTankState {

    float heading = 0.0f;
    float speed = 0.0f;
    tank_drive::Chassis chassis;  // turret handle (chassis.turret) + turret aim
    cannon_fire::State cannon;    // the shared "fire the cannon" sequence state
    teleport::State teleport;     // the "blink" teleport effect (bubble + jump)

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

    // Blink gate: sim_time the tank may next teleport, so a persistently-committed
    // BLINK qubit (index 4) can't chain jumps -- one blink per kBlinkCooldown.
    double next_blink_time = 0.0;

    // Last committed disposition of the co-located quantum_agent (if any):
    // -2 = never read, -1 = deliberating, 0/1 = the chosen outcome. We react
    // only on a CHANGE, so the announcement fires once per collapse.
    int8_t last_decision = -2;

    float drive_speed = 6.0f; // remove soon

    // The tank's LEASED squad slot ("[qtank:0]", "[qtank:1]") = its group-agent
    // member index (qubit slot = tank_id + 1). Claimed from the shared roster on
    // DEPLOY (unpark) and released on death/recycle, so it reflects live membership,
    // not birth order. -1 = not currently in the squad (parked reserve, or the squad
    // was full so this tank fights solo). Preserved across rebuilds.
    int tank_id = -1;

    // Commander-only: the member count it last reshaped its group agent to (-1 =
    // never). The group is now a FIXED size (hub + tank slots + reinforce), so this
    // is really a one-shot "have I reshaped yet" latch rather than a live count.
    int squad_size = -1;

    // Commander-only: EMA of the player's speed the PRESS/HARASS order reads, so the
    // order tracks SUSTAINED movement rather than per-frame jitter (order stability).
    float order_speed_ema = 0.0f;

    // Commander-only: the order state machine's phase (see OrderPhase) + the squad
    // deficit captured at the last rearm. A change in the deficit is one of the
    // triggers that re-opens the order (so the reinforce qubit re-deliberates); the
    // sentinel forces the first frame to rearm and kick off deliberation.
    OrderPhase order_phase = OrderPhase::Holding;
    int order_deficit_at_rearm = -999;

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