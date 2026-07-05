#pragma once
// behavior/projectile_impact.h
//
// The projectile's own per-instance collision state. The "projectile" node carries a
// "projectile_impact" behavior (projectile_impact_plugin.cpp) that, on
// COLLISION_ENTER, records hit=1 plus the world strike position. The shooter's
// cannon_fire polls this each frame via
//     wz_instance_state_of<projectile_impact::State>(facts, projectile, kModule)
// to FREEZE + BURST the projectile at the strike, and clears hit at each launch.
// (The tank it struck logs the hit independently, from its own hitbox handler.)
//
// Trivially copyable: behavior instance state is preserved as raw bytes across
// hot-reloads, so it holds plain data only -- no pointers / RAII members.

#include <stdint.h>

namespace projectile_impact
{
    struct State
    {
        uint8_t hit = 0;                          // 1 = struck a target since launch
        float   hx = 0.0f, hy = 0.0f, hz = 0.0f;  // world strike position
    };

    inline constexpr const char* kModule = "projectile_impact";
}
