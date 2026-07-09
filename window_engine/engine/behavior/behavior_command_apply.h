#pragma once

// engine/behavior/behavior_command_apply.h

#include <engine/behavior/behavior_commands.h>
#include <engine/assets/scene/scene_instance.h>
#include <engine/motion/motion_filter.h>

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace wz::engine::collision
{
    struct CollisionFrameStorage;
}

namespace wz::engine::behavior
{
    uint32_t apply_behavior_commands(
        wz::engine::assets::SceneInstance& scene,
        std::span<const BehaviorCommand> commands,
        std::vector<wz::scene::RuntimeEntityId>* out_changed_entities =
            nullptr);

    uint32_t integrate_motion(
        wz::engine::assets::SceneInstance& scene,
        float delta_seconds,
        std::vector<wz::scene::RuntimeEntityId>* out_changed_entities =
            nullptr);

    uint32_t apply_terrain_constraints(
        wz::engine::assets::SceneInstance& scene,
        const wz::engine::collision::CollisionFrameStorage& collision,
        float delta_seconds,
        std::vector<wz::scene::RuntimeEntityId>* out_changed_entities =
            nullptr);

    // Motion Filter pass: for every node carrying a Motion Filter component, ease
    // its persistent state toward the node's freshly-propagated (rigid) world and
    // OVERWRITE the node's world matrix in place -- so the camera view, render,
    // and audio (all of which read the polytree) see the damped pose. Run as the
    // LAST transform step of the tick, AFTER integrate_motion + terrain + the
    // final propagate_all: it reads node.world as the clean target and never
    // touches node.local, so next frame's propagate resets the target cleanly (no
    // cross-frame windup). `states` is the host-owned per-node state map keyed by
    // stable authored id (survives scene rebuilds); a node absent from it seeds on
    // first sight. `collision` feeds the optional terrain-floor clamp. Returns the
    // number of nodes whose world was changed. NOTE (v1): a filtered node's own
    // world is damped, but its descendants are not re-propagated under the damped
    // pose -- correct for a leaf follower (a camera), a documented gap otherwise.
    uint32_t apply_motion_filters(
        wz::engine::assets::SceneInstance& scene,
        const wz::engine::collision::CollisionFrameStorage& collision,
        float delta_seconds,
        std::unordered_map<
            wz::scene::AuthoredEntityId,
            wz::engine::motion::MotionFilterState>& states);

    uint32_t integrate_linear_velocity(
        wz::engine::assets::SceneInstance& scene,
        float delta_seconds,
        std::vector<wz::scene::RuntimeEntityId>* out_changed_entities =
            nullptr);
}
