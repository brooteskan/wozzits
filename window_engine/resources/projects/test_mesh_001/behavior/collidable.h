#pragma once
// behavior/collidable.h
//
// A collidable's IDENTITY + last-collision record, held as behavior instance state.
// Any node that can be struck carries a "collidable" behavior; a striker (or the
// victim) reads it off a collision event's `other` handle via
//     wz_instance_state_of<collidable::State>(facts, wz_other(event), kModule)
// to learn WHAT it is, WHO owns it, and how much damage it deals. Everything past
// the handle is behavior convention -- the engine defines no schema.
//
// kind/damage/team are AUTHORED in the behavior's config (read in init). owner is a
// runtime handle stamped by the owning behavior (a projectile's shooter stamps its
// own tank root; a child hit-collider would stamp its logical root). hit/hx/hy/hz
// are the last collision, recorded by this behavior for the owner to read (e.g. a
// shooter freezing its projectile at the strike point).
//
// Trivially copyable: instance state is preserved as raw bytes across hot-reloads,
// so it holds plain data only.

#include <engine/behavior/behavior_module_api.h>

namespace collidable
{
    enum Kind : uint32_t
    {
        Unknown    = 0,
        Tank       = 1,
        Projectile = 2,
    };

    struct State
    {
        uint32_t kind = Unknown;                                // collidable::Kind
        WzBehaviorEntityId owner = WZ_INVALID_BEHAVIOR_ENTITY;  // logical owner (shooter / root)
        float    damage = 0.0f;                                 // dealt to whatever it strikes
        int32_t  team = 0;                                      // friendly-fire bucket (0 = neutral)
        uint8_t  hit = 0;                                       // 1 = struck something since launch
        float    hx = 0.0f, hy = 0.0f, hz = 0.0f;              // world strike position
    };

    inline constexpr const char* kModule = "collidable";
}
