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

    // Self-paced cognition.tick dispatch. Fires WZ_EVENT_COGNITION_TICK to each
    // subscribed binding whose scheduled wake (scene.behavior_state.next_wakes,
    // preserved across rebuilds) is due at context.sim_time -- a binding with no
    // entry is due immediately (its first think). Each fired binding is PARKED
    // (next wake set to +infinity) before its handler runs, so a handler that does
    // not call wz_set_next_wake sleeps rather than busy-firing every frame. Unlike
    // the one-shot lifecycle passes this APPENDS to the command buffer (it shares
    // the frame buffer) and does not clear it. Run once per frame, after
    // dispatch_behaviors, before applying the command buffer.
    void dispatch_cognition_tick(
        wz::engine::assets::SceneInstance& scene,
        const BehaviorRegistry& registry,
        BehaviorFrameContext& context);
}
