#pragma once

// engine/behavior/behavior_dispatch.h

#include <engine/behavior/behavior_registry.h>
#include <engine/assets/scene/scene_instance.h>

namespace wz::engine::behavior
{
    // Run each behavior binding's on_init. When skip_initialized is true (an
    // APPEND-ONLY rebuild such as a prefab spawn, where survivor runtime ids stay
    // stable), bindings already in scene.behavior_state.initialized_bindings are
    // skipped and only newly materialized bindings init -- the #257 B1 fix for the
    // per-spawn cost of re-running every survivor's on_init. Default false = init all
    // (initial load, and the renumbering delete/reparent paths).
    void initialize_behaviors(
        wz::engine::assets::SceneInstance& scene,
        const BehaviorRegistry& registry,
        wz::Logger* logger = nullptr,
        bool skip_initialized = false);

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

    // Dispatch a spawn-with-identity completion to the SPAWNER node's subscribed
    // modules (WZ_EVENT_SPAWN_COMPLETED, or _FAILED when payload.status ==
    // WZ_SPAWN_STATUS_FAILED), with facts.active_spawn_event set to `payload`
    // during the dispatch. Run at the frame boundary after the spawn materialized;
    // the caller applies the produced command buffer. #252 pooling.
    void dispatch_spawn_event(
        wz::engine::assets::SceneInstance& scene,
        const BehaviorRegistry& registry,
        BehaviorFrameContext& context,
        wz::scene::RuntimeEntityId spawner,
        const WzSpawnEventPayload& payload);

    void dispatch_behavior_event(
        wz::engine::assets::SceneInstance& scene,
        const BehaviorRegistry& registry,
        BehaviorFrameContext& context,
        BehaviorEvent event);

    // Whether ANY behavior in the scene subscribes to `kind` (module on_event
    // channel accepts it). Recomputed on rebuild + used to skip a per-frame event
    // scan entirely when nothing listens -- e.g. the WZ_EVENT_SELF_ACTIVATED edge
    // scan is zero-cost when no behavior wires self.activated (#252 pooling).
    bool scene_has_event_subscriber(
        const wz::engine::assets::SceneInstance& scene,
        const BehaviorRegistry& registry,
        WzBehaviorEventKind kind);

    // Self-paced cognition.tick dispatch. Fires WZ_EVENT_COGNITION_TICK to each
    // subscribed binding whose scheduled wake (scene.behavior_state.next_wakes,
    // preserved across rebuilds) is due at context.sim_time -- a binding with no
    // entry is due immediately (its first think). Each fired binding is PARKED
    // (next wake set to +infinity) before its handler runs, so a handler that does
    // not call wz_set_next_wake sleeps rather than busy-firing every frame. Unlike
    // the one-shot lifecycle passes this APPENDS to the command buffer (it shares
    // the frame buffer) and does not clear it. Run once per frame, after
    // dispatch_behaviors, before applying the command buffer.
    //
    // BUDGETED: due bindings fire oldest-overdue first until
    // context.cognition_tick_budget_ms of wall-clock is spent, then the remainder
    // are left DUE (wake untouched) and run on a later frame. At least one always
    // fires, so a single agent costing more than the whole budget still makes
    // progress instead of being deferred forever. Deferral is logged as a warning
    // when a logger is wired. Set the budget <= 0 to fire everything due.
    void dispatch_cognition_tick(
        wz::engine::assets::SceneInstance& scene,
        const BehaviorRegistry& registry,
        BehaviorFrameContext& context);
}
