#pragma once
// behavior/agent_tank.h

#include <engine/behavior/behavior_module_api.h>
#include "tank_drive.h"

struct QuantumTankState {

    float heading = 0.0f;
    float speed = 0.0f;
    tank_drive::Chassis chassis;  // turret handle + turret aim
    
    uint8_t ammo = 10;

    WzBehaviorEntityId terrain = WZ_INVALID_BEHAVIOR_ENTITY;
    WzBehaviorEntityId canon_audio = WZ_INVALID_BEHAVIOR_ENTITY;
    WzBehaviorEntityId player = WZ_INVALID_BEHAVIOR_ENTITY;

    WzBehaviorEntityId target = WZ_INVALID_BEHAVIOR_ENTITY;

    // The hidden "command" node whose quantum_agent decides the squad's group
    // order (PRESS/HARASS). A tank READS it and folds it into its own goals; the
    // commander behavior itself leaves this invalid (it IS the command node).
    WzBehaviorEntityId command = WZ_INVALID_BEHAVIOR_ENTITY;

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

    // Last committed disposition of the co-located quantum_agent (if any):
    // -2 = never read, -1 = deliberating, 0/1 = the chosen outcome. We react
    // only on a CHANGE, so the announcement fires once per collapse.
    int8_t last_decision = -2;

    float drive_speed = 6.0f; // remove soon

    // Small stable per-instance id for legible logging ("[qtank:0]", "[qtank:1]").
    // Assigned once on first init; -1 = unassigned (preserved across rebuilds).
    int tank_id = -1;
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