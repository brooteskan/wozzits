#pragma once
// behavior/player_tank.h
#include <engine/behavior/behavior_module_api.h>

struct PlayerTankState {
    float throttle = 0.0f;
    float turn = 0.0f;
    float left_tread_speed = 0.0f;
    float right_tread_speed = 0.0f;

    uint8_t ammo = 10;
    WzBehaviorEntityId terrain = WZ_INVALID_BEHAVIOR_ENTITY;
    WzBehaviorEntityId canon_audio = WZ_INVALID_BEHAVIOR_ENTITY;

    // Last committed disposition of the co-located quantum_agent (if any):
    // -2 = never read, -1 = deliberating, 0/1 = the chosen outcome. We react
    // only on a CHANGE, so the announcement fires once per collapse.
    int8_t last_decision = -2;
};