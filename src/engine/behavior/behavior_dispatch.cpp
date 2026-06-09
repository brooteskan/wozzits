#include <engine/behavior/behavior_dispatch.h>

#include <engine/frame_storage.h>

#include <scene/scene_graph.h>

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
        wz::Logger* logger)
    {
        BehaviorFrameContext context{
            .scene = &scene,
            .behavior_state = &scene.behavior_state,
            .logger = logger,
        };

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
        dispatch_gpu_compute_events_to_modules(scene, registry, context);
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
}
