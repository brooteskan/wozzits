#pragma once
// behavior/player_tank.h
#include <engine/behavior/behavior_module_api.h>
#include "tank_drive.h"

struct PlayerTankState {
    float throttle = 0.0f;
    float turn = 0.0f;
    float left_tread_speed = 0.0f;
    float right_tread_speed = 0.0f;

    uint8_t rpm_level = 0; // [0..4] 0 = off
                        // 1 = Engines_C.wav
                        // 2 = Engines_D.wav
                        // 3 = Engines_F.wav
                        // 4 = Engines_CC.wav
    int8_t applied_rpm_level = -1;  // last engine program pushed (-1 = none yet)

    tank_drive::Chassis chassis;  // turret handle + turret aim

    uint8_t ammo = 10;
    WzBehaviorEntityId terrain = WZ_INVALID_BEHAVIOR_ENTITY;
    WzBehaviorEntityId canon_audio = WZ_INVALID_BEHAVIOR_ENTITY;
    WzBehaviorEntityId engine_audio = WZ_INVALID_BEHAVIOR_ENTITY;

    // Last committed disposition of the co-located quantum_agent (if any):
    // -2 = never read, -1 = deliberating, 0/1 = the chosen outcome. We react
    // only on a CHANGE, so the announcement fires once per collapse.
    int8_t last_decision = -2;
};