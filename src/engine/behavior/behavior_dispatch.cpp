#include <engine/behavior/behavior_dispatch.h>

#include <engine/frame_storage.h>

#include <scene/scene_graph.h>

#include <limits>

namespace wz::engine::behavior
{
    namespace
    {
        WzBehaviorEventKind collision_event_kind(
            wz::engine::collision::CollisionEventKind kind) noexcept
        {
            using Kind = wz::engine::collision::CollisionEventKind;
            switch (kind) {
            case Kind::Enter:
                return WZ_EVENT_COLLISION_ENTER;
            case Kind::Stay:
                return WZ_EVENT_COLLISION_STAY;
            case Kind::Exit:
                return WZ_EVENT_COLLISION_EXIT;
            }
            return WZ_EVENT_NONE;
        }

        WzBehaviorEventKind proximity_event_kind(
            wz::engine::collision::ProximityEventKind kind) noexcept
        {
            using Kind = wz::engine::collision::ProximityEventKind;
            switch (kind) {
            case Kind::Enter:
                return WZ_EVENT_PROXIMITY_ENTER;
            case Kind::Stay:
                return WZ_EVENT_PROXIMITY_STAY;
            case Kind::Exit:
                return WZ_EVENT_PROXIMITY_EXIT;
            }
            return WZ_EVENT_NONE;
        }

        WzBehaviorEventKind input_event_kind(
            WzBehaviorEventKind kind) noexcept
        {
            switch (kind) {
            case WZ_EVENT_INPUT_KEY_PRESSED:
            case WZ_EVENT_INPUT_KEY_RELEASED:
            case WZ_EVENT_INPUT_MOUSE_BUTTON_PRESSED:
            case WZ_EVENT_INPUT_MOUSE_BUTTON_RELEASED:
            case WZ_EVENT_INPUT_CONTROLLER_BUTTON_PRESSED:
            case WZ_EVENT_INPUT_CONTROLLER_BUTTON_RELEASED:
            case WZ_EVENT_INPUT_CONTROLLER_AXIS_CHANGED:
                return kind;
            default:
                return WZ_EVENT_NONE;
            }
        }

        WzBehaviorEventKind gpu_compute_event_kind(
            WzBehaviorEventKind kind) noexcept
        {
            switch (kind) {
            case WZ_EVENT_GPU_COMPUTE_COMPLETED:
            case WZ_EVENT_GPU_COMPUTE_FAILED:
                return kind;
            default:
                return WZ_EVENT_NONE;
            }
        }

        struct ActiveBehaviorScope
        {
            BehaviorFrameContext& context;
            const wz::engine::assets::BehaviorComponent* previous = nullptr;
            wz::scene::RuntimeEntityId previous_entity =
                wz::scene::INVALID_RUNTIME_ENTITY;

            ActiveBehaviorScope(
                BehaviorFrameContext& context_in,
                const wz::engine::assets::BehaviorComponent& behavior,
                wz::scene::RuntimeEntityId entity)
                : context(context_in)
                , previous(context_in.active_behavior)
                , previous_entity(context_in.active_entity)
            {
                context.active_behavior = &behavior;
                context.active_entity = entity;
            }

            ~ActiveBehaviorScope()
            {
                context.active_behavior = previous;
                context.active_entity = previous_entity;
            }
        };

        struct ActiveInputPayloadScope
        {
            BehaviorFrameContext& context;
            const WzInputEventPayload* previous = nullptr;

            ActiveInputPayloadScope(
                BehaviorFrameContext& context_in,
                const WzInputEventPayload& payload)
                : context(context_in)
                , previous(context_in.active_input_payload)
            {
                context.active_input_payload = &payload;
            }

            ~ActiveInputPayloadScope()
            {
                context.active_input_payload = previous;
            }
        };

        struct ActiveGpuComputePayloadScope
        {
            BehaviorFrameContext& context;
            const WzGpuComputeEventPayload* previous = nullptr;

            ActiveGpuComputePayloadScope(
                BehaviorFrameContext& context_in,
                const WzGpuComputeEventPayload& payload)
                : context(context_in)
                , previous(context_in.active_gpu_compute_payload)
            {
                context.active_gpu_compute_payload = &payload;
            }

            ~ActiveGpuComputePayloadScope()
            {
                context.active_gpu_compute_payload = previous;
            }
        };

        struct ActiveSpawnPayloadScope
        {
            BehaviorFrameContext& context;
            const WzSpawnEventPayload* previous = nullptr;

            ActiveSpawnPayloadScope(
                BehaviorFrameContext& context_in,
                const WzSpawnEventPayload& payload)
                : context(context_in)
                , previous(context_in.active_spawn_payload)
            {
                context.active_spawn_payload = &payload;
            }

            ~ActiveSpawnPayloadScope()
            {
                context.active_spawn_payload = previous;
            }
        };

        bool behavior_accepts_event(
            const BehaviorRegistry& registry,
            const wz::engine::assets::BehaviorComponent& component,
            WzBehaviorEventKind kind)
        {
            if (!component.enabled || component.module.empty()) {
                return false;
            }

            const auto module_handle = registry.find_module(component.module);
            if (!module_handle) {
                return false;
            }

            const BehaviorModuleRegistration* module =
                registry.get_module(*module_handle);
            if (!module || !module->on_event) {
                return false;
            }

            const EventChannelMask mask =
                component.events.empty()
                    ? module->default_channel_mask
                    : component.channel_mask;
            return channel_mask_accepts_event(mask, kind);
        }

        void dispatch_module_event(
            const BehaviorRegistry& registry,
            BehaviorFrameContext& context,
            const wz::engine::assets::BehaviorComponent& component,
            const BehaviorEvent& event)
        {
            if (!component.enabled || component.module.empty()) {
                return;
            }
            // "Live?" gate (#252): skip a behavior on a hierarchically-inactive
            // (parked/pooled) node, independent of visibility. The single chokepoint
            // every module event routes through (frame-update / input / collision /
            // proximity / gpu-compute / self-start / cognition-tick), so one check
            // gates them all. Empty mask => all live (back-compat with callers that
            // never populate it). Orthogonal to component.enabled (per-behavior);
            // `active` is the whole-node axis.
            if (context.scene
                && !context.scene->entity_is_active(event.entity)) {
                return;
            }

            const auto module_handle = registry.find_module(component.module);
            if (!module_handle) {
                return;
            }

            const BehaviorModuleRegistration* module =
                registry.get_module(*module_handle);
            if (!module || !module->on_event) {
                return;
            }

            ActiveBehaviorScope active_behavior(
                context,
                component,
                event.entity);
            module->on_event(context, event, module->user_data);
        }

        void dispatch_matching_module_event(
            const BehaviorRegistry& registry,
            BehaviorFrameContext& context,
            const wz::engine::assets::BehaviorComponent& component,
            const BehaviorEvent& event)
        {
            if (!behavior_accepts_event(registry, component, event.kind)) {
                return;
            }
            dispatch_module_event(registry, context, component, event);
        }

        void dispatch_frame_update_events_to_modules(
            const wz::engine::assets::SceneInstance& scene,
            const BehaviorRegistry& registry,
            BehaviorFrameContext& context)
        {
            for (const auto& record : scene.behaviors) {
                const BehaviorEvent event{
                    .kind = WZ_EVENT_FRAME_UPDATE,
                    .entity = record.node,
                    .other = wz::scene::INVALID_RUNTIME_ENTITY,
                    .self_is_trigger = false,
                };
                dispatch_matching_module_event(
                    registry,
                    context,
                    record.component,
                    event);
            }
        }

        void dispatch_scene_loaded_events_to_modules(
            const wz::engine::assets::SceneInstance& scene,
            const BehaviorRegistry& registry,
            BehaviorFrameContext& context)
        {
            for (const auto& record : scene.behaviors) {
                const BehaviorEvent event{
                    .kind = WZ_EVENT_SCENE_LOADED,
                    .entity = record.node,
                    .other = wz::scene::INVALID_RUNTIME_ENTITY,
                    .self_is_trigger = false,
                };
                dispatch_matching_module_event(
                    registry,
                    context,
                    record.component,
                    event);
            }
        }

        void dispatch_collision_events_to_modules(
            const wz::engine::assets::SceneInstance& scene,
            const BehaviorRegistry& registry,
            BehaviorFrameContext& context)
        {
            if (!context.frame_storage) {
                return;
            }

            const auto entity_events =
                !context.frame_storage->collision.entity_events.empty()
                    ? std::span<const wz::engine::collision::CollisionEntityEvent>(
                        context.frame_storage->collision.entity_events)
                    : std::span<const wz::engine::collision::CollisionEntityEvent>(
                        context.frame_storage->collision.routed_entity_events);

            for (const auto& collision_event : entity_events)
            {
                const BehaviorEvent event{
                    .kind = collision_event_kind(collision_event.kind),
                    .entity = collision_event.entity,
                    .other = collision_event.other,
                    .self_is_trigger = collision_event.self_is_trigger,
                };
                if (event.kind == WZ_EVENT_NONE) {
                    continue;
                }

                for (const auto& record : scene.behaviors) {
                    if (record.node != collision_event.entity) {
                        continue;
                    }
                    dispatch_matching_module_event(
                        registry,
                        context,
                        record.component,
                        event);
                }
            }
        }

        void dispatch_input_events_to_modules(
            const wz::engine::assets::SceneInstance& scene,
            const BehaviorRegistry& registry,
            BehaviorFrameContext& context)
        {
            if (!context.frame_storage) {
                return;
            }

            if (!context.frame_storage->input_events.events.empty()) {
                for (const auto& input_event :
                    context.frame_storage->input_events.events)
                {
                    const BehaviorEvent event{
                        .kind = input_event_kind(input_event.kind),
                        .other = wz::scene::INVALID_RUNTIME_ENTITY,
                        .self_is_trigger = false,
                    };
                    if (event.kind == WZ_EVENT_NONE) {
                        continue;
                    }

                    ActiveInputPayloadScope active_payload(
                        context,
                        input_event.payload);
                    for (const auto& record : scene.behaviors) {
                        BehaviorEvent routed_event = event;
                        routed_event.entity = record.node;
                        dispatch_matching_module_event(
                            registry,
                            context,
                            record.component,
                            routed_event);
                    }
                }
                return;
            }

            for (const auto& input_event :
                context.frame_storage->input_events.routed_entity_events)
            {
                const BehaviorEvent event{
                    .kind = input_event_kind(input_event.kind),
                    .entity = input_event.entity,
                    .other = wz::scene::INVALID_RUNTIME_ENTITY,
                    .self_is_trigger = false,
                };
                if (event.kind == WZ_EVENT_NONE) {
                    continue;
                }

                ActiveInputPayloadScope active_payload(
                    context,
                    input_event.payload);
                for (const auto& record : scene.behaviors) {
                    if (record.node != input_event.entity) {
                        continue;
                    }
                    dispatch_matching_module_event(
                        registry,
                        context,
                        record.component,
                        event);
                }
            }
        }

        void dispatch_proximity_events_to_modules(
            const wz::engine::assets::SceneInstance& scene,
            const BehaviorRegistry& registry,
            BehaviorFrameContext& context)
        {
            if (!context.frame_storage) {
                return;
            }

            const auto entity_events =
                !context.frame_storage->collision.proximity_entity_events.empty()
                    ? std::span<const wz::engine::collision::ProximityEntityEvent>(
                        context.frame_storage->collision.proximity_entity_events)
                    : std::span<const wz::engine::collision::ProximityEntityEvent>(
                        context.frame_storage->collision.routed_proximity_entity_events);

            for (const auto& proximity_event : entity_events)
            {
                const BehaviorEvent event{
                    .kind = proximity_event_kind(proximity_event.kind),
                    .entity = proximity_event.entity,
                    .other = proximity_event.other,
                    .self_is_trigger = false,
                };
                if (event.kind == WZ_EVENT_NONE) {
                    continue;
                }

                for (const auto& record : scene.behaviors) {
                    if (record.node != proximity_event.entity) {
                        continue;
                    }
                    dispatch_matching_module_event(
                        registry,
                        context,
                        record.component,
                        event);
                }
            }
        }

        void dispatch_gpu_compute_events_to_modules(
            const wz::engine::assets::SceneInstance& scene,
            const BehaviorRegistry& registry,
            BehaviorFrameContext& context)
        {
            if (!context.gpu_compute) {
                return;
            }

            std::vector<BehaviorGpuComputeEvent> events;
            events.swap(context.gpu_compute->events);
            for (const auto& gpu_event : events) {
                std::vector<WzGpuComputeOutputView> output_views;
                output_views.reserve(gpu_event.outputs.size());
                for (const BehaviorGpuPortValue& output :
                    gpu_event.outputs)
                {
                    output_views.push_back(WzGpuComputeOutputView{
                        .name = output.name.c_str(),
                        .kind = output.kind,
                        .element_count = output.element_count,
                        .stride_bytes = output.stride_bytes,
                        .bytes = output.initial_data.data(),
                        .byte_count =
                            static_cast<uint64_t>(
                                output.initial_data.size()),
                    });
                }
                WzGpuComputeEventPayload payload = gpu_event.payload;
                payload.output_count =
                    static_cast<uint32_t>(output_views.size());
                payload.outputs = output_views.empty()
                    ? nullptr
                    : output_views.data();
                const BehaviorEvent event{
                    .kind = gpu_compute_event_kind(gpu_event.kind),
                    .entity = gpu_event.entity,
                    .other = wz::scene::INVALID_RUNTIME_ENTITY,
                    .self_is_trigger = false,
                };
                if (event.kind == WZ_EVENT_NONE) {
                    continue;
                }

                ActiveGpuComputePayloadScope active_payload(
                    context,
                    payload);
                for (const auto& record : scene.behaviors) {
                    if (record.node != gpu_event.entity) {
                        continue;
                    }
                    dispatch_matching_module_event(
                        registry,
                        context,
                        record.component,
                        event);
                }
            }
        }

        void dispatch_event_to_modules(
            const wz::engine::assets::SceneInstance& scene,
            const BehaviorRegistry& registry,
            BehaviorFrameContext& context,
            const BehaviorEvent& event)
        {
            if (event.kind == WZ_EVENT_NONE
                || event.entity == wz::scene::INVALID_RUNTIME_ENTITY)
            {
                return;
            }

            for (const auto& record : scene.behaviors) {
                if (record.node != event.entity) {
                    continue;
                }
                dispatch_matching_module_event(
                    registry,
                    context,
                    record.component,
                    event);
            }
        }
    }

    void initialize_behaviors(
        wz::engine::assets::SceneInstance& scene,
        const BehaviorRegistry& registry,
        wz::Logger* logger,
        bool skip_initialized)
    {
        BehaviorFrameContext context{
            .scene = &scene,
            .behavior_state = &scene.behavior_state,
            .logger = logger,
            .registry = &registry,
        };

        auto& initialized = scene.behavior_state.initialized_bindings;

        for (const auto node : wz::core::graph::topo_order(
                 scene.storage.polytree))
        {
            for (const auto& record : scene.behaviors) {
                const auto& component = record.component;
                if (record.node != node
                    || !component.enabled
                    || component.module.empty())
                {
                    continue;
                }

                // Init-scoping (#257 B1): on an APPEND-ONLY rebuild (a prefab spawn),
                // a binding already initialized in a prior rebuild keeps its runtime
                // id + cached handles (the spawn only appends, so survivor ids are
                // stable) and its preserved instance state, so re-running its on_init
                // is pure waste -- re-walking the scene for find_entity, rebuilding a
                // quantum agent, etc. Skip it; only newly materialized bindings init.
                // This mirrors the started_bindings gate that already scopes self.start.
                // When skip_initialized is false (load / delete / reparent -- paths that
                // RENUMBER, so survivors' cached handles must be re-resolved) every
                // binding re-inits as before.
                if (skip_initialized
                    && !component.binding_id.empty()
                    && initialized.count(component.binding_id) != 0u)
                {
                    continue;
                }

                const auto module_handle =
                    registry.find_module(component.module);
                if (!module_handle) {
                    continue;
                }

                const BehaviorModuleRegistration* module =
                    registry.get_module(*module_handle);
                if (!module || !module->on_init) {
                    continue;
                }

                ActiveBehaviorScope active_behavior(
                    context,
                    component,
                    record.node);
                module->on_init(context, record.node, module->user_data);
                if (!component.binding_id.empty()) {
                    initialized.insert(component.binding_id);
                }
            }
        }
    }

    void dispatch_behaviors(
        wz::engine::assets::SceneInstance& scene,
        const BehaviorRegistry& registry,
        BehaviorFrameContext& context)
    {
        if (!context.commands) {
            return;
        }

        if (!context.scene) {
            context.scene = &scene;
        }
        if (!context.behavior_state) {
            context.behavior_state = &scene.behavior_state;
        }
        // So facts.actuator_lookup can resolve behavior-registered actuators the
        // statechart runner calls this frame (the open actuator vocabulary).
        context.registry = &registry;

        context.commands->clear();
        if (context.gpu_compute) {
            context.gpu_compute->clear_jobs();
        }
        // Command order is deterministic: subscribed module frame.update
        // events, routed input/collision/proximity module events, then legacy
        // named functions.
        dispatch_frame_update_events_to_modules(scene, registry, context);
        dispatch_input_events_to_modules(scene, registry, context);
        dispatch_collision_events_to_modules(scene, registry, context);
        dispatch_proximity_events_to_modules(scene, registry, context);
        dispatch_gpu_compute_events_to_modules(scene, registry, context);

        for (const auto& record : scene.behaviors) {
            const auto& component = record.component;
            if (!component.enabled || component.name.empty()) {
                continue;
            }
            // "Live?" gate (#252): the named-function path does not route through
            // dispatch_module_event, so gate it here too.
            if (!scene.entity_is_active(record.node)) {
                continue;
            }

            const auto handle = registry.find(
                component.module,
                component.name);
            if (!handle) {
                continue;
            }

            const BehaviorRegistration* registration =
                registry.get(*handle);
            if (!registration || !registration->function) {
                continue;
            }

            ActiveBehaviorScope active_behavior(
                context,
                component,
                record.node);
            registration->function(
                context,
                record.node,
            registration->user_data);
        }
    }

    void dispatch_behavior_gpu_compute_events(
        wz::engine::assets::SceneInstance& scene,
        const BehaviorRegistry& registry,
        BehaviorFrameContext& context)
    {
        if (!context.scene) {
            context.scene = &scene;
        }
        if (!context.behavior_state) {
            context.behavior_state = &scene.behavior_state;
        }
        context.registry = &registry;
        dispatch_gpu_compute_events_to_modules(scene, registry, context);
    }

    bool scene_has_event_subscriber(
        const wz::engine::assets::SceneInstance& scene,
        const BehaviorRegistry& registry,
        WzBehaviorEventKind kind)
    {
        for (const auto& record : scene.behaviors) {
            if (behavior_accepts_event(registry, record.component, kind)) {
                return true;
            }
        }
        return false;
    }

    void dispatch_behavior_event(
        wz::engine::assets::SceneInstance& scene,
        const BehaviorRegistry& registry,
        BehaviorFrameContext& context,
        BehaviorEvent event)
    {
        if (!context.scene) {
            context.scene = &scene;
        }
        if (!context.behavior_state) {
            context.behavior_state = &scene.behavior_state;
        }
        dispatch_event_to_modules(scene, registry, context, event);
    }

    void dispatch_scene_loaded(
        wz::engine::assets::SceneInstance& scene,
        const BehaviorRegistry& registry,
        BehaviorFrameContext& context)
    {
        if (!context.commands) {
            return;
        }
        if (!context.scene) {
            context.scene = &scene;
        }
        if (!context.behavior_state) {
            context.behavior_state = &scene.behavior_state;
        }

        // One-shot lifecycle pass: dispatch WZ_EVENT_SCENE_LOADED to subscribed
        // modules only (no per-frame frame.update/input/etc., and not the legacy
        // named-function loop). Callers run this once after the scene is
        // materialized, before the frame loop, and apply the produced commands.
        context.commands->clear();
        dispatch_scene_loaded_events_to_modules(scene, registry, context);
    }

    void dispatch_self_start(
        wz::engine::assets::SceneInstance& scene,
        const BehaviorRegistry& registry,
        BehaviorFrameContext& context)
    {
        if (!context.commands) {
            return;
        }
        if (!context.scene) {
            context.scene = &scene;
        }
        if (!context.behavior_state) {
            context.behavior_state = &scene.behavior_state;
        }

        // One-shot lifecycle pass: dispatch WZ_EVENT_SELF_START to subscribed
        // modules only, and only for bindings that have not started yet. The
        // started set lives in behavior_state (preserved across rebuilds), so on
        // a spawn this fires only for the newly materialized bindings -- existing
        // actors keep their flag and are not re-notified. Marks each fired binding
        // started. Callers apply the produced commands.
        context.commands->clear();
        auto& started = scene.behavior_state.started_bindings;
        for (const auto& record : scene.behaviors) {
            const auto& component = record.component;
            if (component.binding_id.empty()) {
                continue;  // no stable key to dedupe on
            }
            if (!behavior_accepts_event(
                    registry, component, WZ_EVENT_SELF_START))
            {
                continue;
            }
            if (started.find(component.binding_id) != started.end()) {
                continue;  // already started
            }

            const BehaviorEvent event{
                .kind = WZ_EVENT_SELF_START,
                .entity = record.node,
                .other = wz::scene::INVALID_RUNTIME_ENTITY,
                .self_is_trigger = false,
            };
            // Mark started only if the node is actually live -- an inactive node's
            // self.start is dropped by the dispatch gate (same condition), so marking
            // it started would lose the event forever (dispatch_self_start skips
            // started bindings). Leaving it unstarted lets self.start fire on unpark.
            const bool node_live =
                !context.scene
                || context.scene->entity_is_active(record.node);
            dispatch_module_event(registry, context, component, event);
            if (node_live) {
                started.insert(component.binding_id);
            }
        }
    }

    void dispatch_spawn_event(
        wz::engine::assets::SceneInstance& scene,
        const BehaviorRegistry& registry,
        BehaviorFrameContext& context,
        wz::scene::RuntimeEntityId spawner,
        const WzSpawnEventPayload& payload)
    {
        if (!context.scene) {
            context.scene = &scene;
        }
        const BehaviorEvent event{
            .kind = static_cast<WzBehaviorEventKind>(
                payload.status == WZ_SPAWN_STATUS_FAILED
                    ? WZ_EVENT_SPAWN_FAILED
                    : WZ_EVENT_SPAWN_COMPLETED),
            .entity = spawner,
            .other = wz::scene::INVALID_RUNTIME_ENTITY,
            .self_is_trigger = false,
        };
        // Route to the spawner's subscribed modules only, payload live for the
        // duration (read via facts.active_spawn_event). Mirrors the gpu-compute
        // completion dispatch. The spawner is a live node, so the dispatch gate
        // passes.
        ActiveSpawnPayloadScope active_payload(context, payload);
        for (const auto& record : scene.behaviors) {
            if (record.node != spawner) {
                continue;
            }
            dispatch_matching_module_event(
                registry, context, record.component, event);
        }
    }

    void dispatch_cognition_tick(
        wz::engine::assets::SceneInstance& scene,
        const BehaviorRegistry& registry,
        BehaviorFrameContext& context)
    {
        if (!context.commands) {
            return;
        }
        if (!context.scene) {
            context.scene = &scene;
        }
        if (!context.behavior_state) {
            context.behavior_state = &scene.behavior_state;
        }

        // Self-paced wakes: fire WZ_EVENT_COGNITION_TICK only for bindings whose
        // scheduled wake is due at the current sim-time. A binding with no entry is
        // due now (its first think); after firing it is parked at +infinity, so a
        // handler that does not call wz_set_next_wake stays asleep instead of
        // busy-firing. This does NOT clear the command buffer -- it shares the
        // frame buffer with the per-frame dispatch.
        constexpr double kDueNow = -std::numeric_limits<double>::infinity();
        constexpr double kAsleep = std::numeric_limits<double>::infinity();
        const double now = context.sim_time;
        auto& state = scene.behavior_state;
        for (const auto& record : scene.behaviors) {
            const auto& component = record.component;
            if (component.binding_id.empty()) {
                continue;  // no stable key to schedule on
            }
            if (!behavior_accepts_event(
                    registry, component, WZ_EVENT_COGNITION_TICK))
            {
                continue;
            }
            // A parked (inactive) node must be skipped BEFORE the park-at-infinity
            // below. The gate in dispatch_module_event would drop the tick, so the
            // handler never runs to reschedule; parking it here would then strand it
            // asleep forever -- it would never tick again even after being unparked.
            // Skipping instead leaves its wake due, so it resumes on unpark.
            if (!scene.entity_is_active(record.node)) {
                continue;
            }
            if (now < state.next_wake_or(component.binding_id, kDueNow)) {
                continue;  // not due yet
            }

            // Park before firing: the handler reschedules via wz_set_next_wake.
            state.set_next_wake(component.binding_id, kAsleep);
            const BehaviorEvent event{
                .kind = WZ_EVENT_COGNITION_TICK,
                .entity = record.node,
                .other = wz::scene::INVALID_RUNTIME_ENTITY,
                .self_is_trigger = false,
            };
            dispatch_module_event(registry, context, component, event);
        }
    }
}
