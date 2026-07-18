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

    // Height + normal of a terrain collider's surface at a world XZ.
    //
    // `observer` matters only for a heightfield whose collision asset opted into
    // drawn-surface reconstruction (constrain_to_drawn_surface): a geometry
    // clipmap draws that field in camera-centred rings, so which ring covers
    // this point -- and therefore how coarse the drawn ground is here -- depends
    // on where the viewer is. Every other collider ignores it, and the default
    // (the origin) is a well-defined observer rather than a missing one.
    //
    // Callers holding a CollisionFrameStorage should pass its
    // observer_world_position; the host publishes it each frame.
    bool sample_terrain_surface(
        const CollisionWorldEntry& entry,
        float world_x,
        float world_z,
        CollisionSurfaceSample& out_sample,
        const wz::math::Vec3& observer = {}) noexcept;

    bool sample_nearest_terrain_surface(
        const CollisionWorldEntry& entry,
        float world_x,
        float world_z,
        CollisionSurfaceSample& out_sample,
        const wz::math::Vec3& observer = {}) noexcept;

    // Ray-cast against a terrain collider's surface, returning the nearest hit
    // within max_distance. Services TerrainHeightField colliders (the
    // geometry-clipmap landscape carries a heightfield, not a triangle mesh, so
    // a shot fired at it would otherwise never register a ground hit); any other
    // shape_kind returns false. The surface height is reconstructed by BILINEAR
    // filtering of the resident height field -- the same reconstruction the
    // clipmap render shader uses -- so the ray strikes the terrain where it is
    // DRAWN, even for a grazing shot skimming a shallow rise. ray_direction need
    // not be normalized; a zero-length direction is rejected.
    bool raycast_terrain_surface(
        const CollisionWorldEntry& entry,
        const wz::math::Vec3& ray_origin,
        const wz::math::Vec3& ray_direction,
        float max_distance,
        CollisionSurfaceSample& out_sample) noexcept;
}
