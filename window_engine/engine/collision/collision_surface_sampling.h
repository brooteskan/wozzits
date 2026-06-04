#pragma once

// engine/collision/collision_surface_sampling.h

#include <engine/collision/collision_frame.h>

namespace wz::engine::collision
{
    struct CollisionSurfaceSample
    {
        bool hit = false;
        wz::scene::RuntimeEntityId surface_entity =
            wz::scene::INVALID_RUNTIME_ENTITY;
        wz::math::Vec3 position{};
        wz::math::Vec3 normal{ .x = 0.0f, .y = 1.0f, .z = 0.0f };
    };

    bool sample_terrain_surface(
        const CollisionWorldEntry& entry,
        float world_x,
        float world_z,
        CollisionSurfaceSample& out_sample) noexcept;
}
