#pragma once

// engine/behavior/behavior_dispatch.h

#include <engine/behavior/behavior_registry.h>
#include <engine/assets/scene/scene_instance.h>

namespace wz::engine::behavior
{
    void initialize_behaviors(
        wz::engine::assets::SceneInstance& scene,
        const BehaviorRegistry& registry,
        wz::Logger* logger = nullptr);

    void dispatch_behaviors(
        wz::engine::assets::SceneInstance& scene,
        const BehaviorRegistry& registry,
        BehaviorFrameContext& context);

    void dispatch_behavior_gpu_compute_events(
        wz::engine::assets::SceneInstance& scene,
        const BehaviorRegistry& registry,
        BehaviorFrameContext& context);

    void dispatch_behavior_event(
        wz::engine::assets::SceneInstance& scene,
        const BehaviorRegistry& registry,
        BehaviorFrameContext& context,
        BehaviorEvent event);
}
