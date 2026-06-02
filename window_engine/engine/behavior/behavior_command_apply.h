#pragma once

// engine/behavior/behavior_command_apply.h

#include <engine/behavior/behavior_commands.h>
#include <engine/assets/scene/scene_instance.h>

#include <cstdint>
#include <span>
#include <vector>

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

    uint32_t integrate_linear_velocity(
        wz::engine::assets::SceneInstance& scene,
        float delta_seconds,
        std::vector<wz::scene::RuntimeEntityId>* out_changed_entities =
            nullptr);
}
