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

    // One-shot WZ_EVENT_SCENE_LOADED dispatch (module on_event subscribers
    // only). Run once after the scene is materialized, before the frame loop;
    // the caller applies the produced command buffer (e.g. SET_ACTIVE_CAMERA).
    void dispatch_scene_loaded(
        wz::engine::assets::SceneInstance& scene,
        const BehaviorRegistry& registry,
        BehaviorFrameContext& context);

    // One-shot WZ_EVENT_SELF_START dispatch (module on_event subscribers only).
    // Fires the event for each subscribed binding that has NOT yet started
    // (tracked in scene.behavior_state.started_bindings, preserved across
    // rebuilds), then marks it started. Run after the scene is (re)materialized;
    // the caller applies the produced command buffer. On a spawn this fires only
    // for the newly added bindings -- existing actors are not re-notified.
    void dispatch_self_start(
        wz::engine::assets::SceneInstance& scene,
        const BehaviorRegistry& registry,
        BehaviorFrameContext& context);

    void dispatch_behavior_event(
        wz::engine::assets::SceneInstance& scene,
        const BehaviorRegistry& registry,
        BehaviorFrameContext& context,
        BehaviorEvent event);
}
