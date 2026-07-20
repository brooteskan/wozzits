#pragma once

#include <engine/behavior/behavior_module_api.h>

// enemy_tank_v1's instance state — the CONTRACT between the body and its levers.
// The body (enemy_tank_v1.cpp) owns and evolves this; the levers
// (enemy_tank_v1_actuators.cpp) write the intent knobs through the peer-state
// channel wz_instance_state_of. Same split as agent_tank.h + tank_actuators.

// The binding's module name: the key wz_instance_state_of resolves the peer state
// by. Declared once, here, so the body and the levers cannot drift.
inline constexpr const char* kEnemyTankModule = "enemy_tank_v1";

struct TankState {
    static constexpr uint8_t kMaxTargets = 4;
    static constexpr uint8_t kNoDepot = 0xFF;

    WzBehaviorEntityId targets[kMaxTargets] = { };
    uint8_t target_count = 0;
    uint8_t depot = kNoDepot;   // PACKED index of the pump, or kNoDepot

    // The two knobs a MIND will take over. `chosen` is writable from a chart
    // via the choose_target actuator (the levers file); throttle's lever comes later.
    uint8_t target_chosen = 0;      // WHICH target
    float   throttle = 0.0f;   // HOW FAST, 0..1

    // Tunables — read ONCE on self.start; the zeroes are throwaways.
    float capacity = 0.0f;
    float idle_burn = 0.0f;   // fuel/sec parked
    float full_burn = 0.0f;   // fuel/sec flat out
    float refuel_radius = 0.0f;
    float refuel_rate = 0.0f;

    float fuel = 0.0f;
    bool stranded = false;      // so "out of fuel" logs once, not 60x/second

    // LEG PROGRESS. A chart's pure ops are stateless, so "ground made up since
    // the last decision" cannot be computed there -- it needs a memory of where
    // the leg began. The body keeps it and publishes the finished leg's total,
    // the same way it publishes fuel: sense here, decide in the chart.
    static constexpr uint8_t kNoLeg = 0xFF;
    float   leg_start_range = 0.0f;   // range to the player when this leg began
    float   last_leg_closed = 0.0f;   // ground gained on the leg that just ended
    uint8_t last_target = kNoLeg;     // which target the current leg drives at
};