#pragma once
// behavior/player_tank.h
#include <engine/behavior/behavior_module_api.h>
#include "tank_drive.h"
#include "cannon_fire.h"
#include "tank_damage.h"
#include "tank_lifecycle.h"

// Barrel elevation travel limits (radians, relative to the gun's LEVEL rest pose
// authored in the GLB). The gun pitches between these while buttons 13 (raise) /
// 10 (lower) are held. Max = raise ceiling above horizontal; min = depression
// floor (0 = level; go negative to let the muzzle drop below horizontal).
inline constexpr float kGunElevationMax = 2 * 0.2617994f;  // 30 deg above horizontal
inline constexpr float kGunElevationMin = 0.0f;        // level

// Total damage the player can absorb before it is destroyed. On death the player
// respawns at its captured spawn point with the tally cleared (it is NOT removed --
// its camera child would go with it). Enemy shells deal 25 (authored on the
// projectile's collidable), so this is ~8 clean hits. Tune to taste.
inline constexpr float kMaxHealth = 200.0f;

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

    tank_drive::Chassis chassis;  // turret handle (chassis.turret) + turret aim
    cannon_fire::State cannon;    // the shared "fire the cannon" sequence state

    // The gun (barrel) node -- the "gun" child of the turret in the tank GLB
    // (body -> turret -> gun). Recorded so the muzzle flash can anchor on it and so
    // the turret can be aimed independently of the barrel later.
    WzBehaviorEntityId barrel = WZ_INVALID_BEHAVIOR_ENTITY;

    // Barrel elevation (rad above horizontal), clamped [0, 15 deg]. Controller
    // buttons 13 (raise) / 10 (lower) set the held flags; the gun pitches per frame.
    float   barrel_elevation = 0.0f;
    uint8_t raise_held = 0;
    uint8_t lower_held = 0;

    uint8_t ammo = 255;
    WzBehaviorEntityId terrain = WZ_INVALID_BEHAVIOR_ENTITY;
    WzBehaviorEntityId canon_audio = WZ_INVALID_BEHAVIOR_ENTITY;
    WzBehaviorEntityId engine_audio = WZ_INVALID_BEHAVIOR_ENTITY;

    // Damage tracking: the "hitbox" child's hit_logger accumulates the tally; the
    // lifecycle machine (Alive/Dead) polls it and respawns at the captured spawn
    // point on death. Spawn capture + the tally poll now live in tank_lifecycle.
    WzBehaviorEntityId hitbox = WZ_INVALID_BEHAVIOR_ENTITY;
    tank_lifecycle::State lifecycle;

    // Last committed disposition of the co-located quantum_agent (if any):
    // -2 = never read, -1 = deliberating, 0/1 = the chosen outcome. We react
    // only on a CHANGE, so the announcement fires once per collapse.
    int8_t last_decision = -2;
};