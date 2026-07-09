#pragma once

// engine/motion/motion_filter.h
//
// The Motion Filter runtime core (issue: secondary-motion camera damping,
// seam 2). Given a node's DRIVEN (rigid) world transform each frame, ease a
// persistent per-node state toward it per degree of freedom and clamp the
// result -- so an attached camera lands in its target pose smoothly (secondary
// motion, so the followed object doesn't look locked in space), can hold a level
// horizon, stay above the terrain, and keep its rotation within limits.
//
// This is the pure, deterministic math (no scene/GPU/sim deps beyond the
// authored SceneMotionFilterAsset + math types), so it unit-tests headless. The
// sim wiring -- reading each filtered node's polytree world, writing the result
// back, propagating to children, mirroring apply_terrain_constraints -- is a
// separate seam.
//
// Frames match the authored intent: translation is smoothed/clamped in WORLD
// axes (vertical bob = world Y, terrain floor = world Y); rotation is decomposed
// into the node's LOCAL roll/pitch/yaw (the engine's quaternion_from_euler_
// degrees convention, so a channel here is the same axis the inspector shows)
// and each is smoothed/leveled/limited independently.

#include <engine/assets/scene/scene_asset_data.h>  // SceneMotionFilterAsset
#include <math/math_types.h>                        // Mat4, Transform, Vec3

#include <functional>
#include <optional>

namespace wz::engine::motion
{
    // Per-node persistent filter state: the smoothed pose plus per-DOF velocity
    // the critically-damped spring carries frame to frame. `initialized` false
    // seeds the state from the target on first application, so a filtered node
    // starts AT its target (no startup lag spike). The caller owns one of these
    // per filtered node, keyed by stable node id so it survives scene rebuilds.
    struct MotionFilterState
    {
        bool initialized = false;
        wz::math::Vec3 position{ 0.0f, 0.0f, 0.0f };
        wz::math::Vec3 position_velocity{ 0.0f, 0.0f, 0.0f };
        // Roll(X) / pitch(Y) / yaw(Z) in degrees + per-axis angular velocity.
        float rotation_euler[3]{ 0.0f, 0.0f, 0.0f };
        float rotation_velocity[3]{ 0.0f, 0.0f, 0.0f };
    };

    // World-Y floor query for the terrain-floor clamp: returns the terrain height
    // under (x, z), or nullopt if there is none. The caller wires it to the
    // terrain sampler (the sim reuses collision's support sampler); an empty
    // function disables the terrain floor.
    using TerrainFloorSampler =
        std::function<std::optional<float>(float x, float z)>;

    // Advance `state` one step (dt seconds) toward `target_world` under `filter`,
    // returning the filtered pose (position + rotation; scale is passed through
    // from the target). A no-op filter (all smoothing 0, no clamps, enabled)
    // returns the target pose, so an untouched component is invisible.
    wz::math::Transform apply_motion_filter(
        const wz::math::Mat4& target_world,
        const wz::engine::assets::SceneMotionFilterAsset& filter,
        MotionFilterState& state,
        float dt,
        const TerrainFloorSampler& terrain = {});
}
