#pragma once
// behavior/tank_damage.h
//
// The tank hitbox's cumulative damage tally, held as the "hit_logger" behavior's
// instance state. hit_logger runs on the child "hitbox" node and accumulates here;
// the tank ROOT polls it via
//     wz_instance_state_of<tank_damage::Tally>(facts, hitbox, kModule)
// each frame to trigger death once the tally passes the tank's kMaxHealth (defined
// per tank type in player_tank.h / agent_tank.h).
//
// Trivially copyable: instance state is preserved as raw bytes across hot-reloads.

#include <stdint.h>

namespace tank_damage
{
    struct Tally
    {
        float total = 0.0f;  // cumulative damage this tank has taken
    };

    inline constexpr const char* kModule = "hit_logger";
}
