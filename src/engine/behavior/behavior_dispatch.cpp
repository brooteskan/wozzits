#include <engine/behavior/behavior_dispatch.h>

#include <engine/frame_storage.h>

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
                return kind;
            default:
                return WZ_EVENT_NONE;
            }
        }

        const wz::engine::assets::BehaviorComponent* behavior_for_entity(
            const wz::engine::assets::SceneInstance& scene,
            wz::scene::RuntimeEntityId entity)
        {
            for (const auto& record : scene.behaviors) {
                if (record.node == entity) {
                    return &record.component;
                }
            }
            return nullptr;
        }

        struct ActiveBehaviorScope
        {
            BehaviorFrameContext& context;
            const wz::engine::assets::BehaviorComponent* previous = nullptr;

            ActiveBehaviorScope(
                BehaviorFrameContext& context_in,
                const wz::engine::assets::BehaviorComponent& behavior)
                : context(context_in)
                , previous(context_in.active_behavior)
            {
                context.active_behavior = &behavior;
            }

            ~ActiveBehaviorScope()
            {
                context.active_behavior = previous;
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

            ActiveBehaviorScope active_behavior(context, component);
            module->on_event(context, event, module->user_data);
        }

        void dispatch_frame_update_events_to_modules(
            const wz::engine::assets::SceneInstance& scene,
            const BehaviorRegistry& registry,
            BehaviorFrameContext& context)
        {
            for (const auto& record : scene.behaviors) {
                bool subscribed = false;
                for (const auto& listener : scene.event_listeners) {
                    if (listener.node == record.node
                        && wz::engine::assets::listener_accepts_event(
                            listener.component,
                            WZ_EVENT_FRAME_UPDATE))
                    {
                        subscribed = true;
                        break;
                    }
                }
                if (!subscribed) {
                    continue;
                }

                const BehaviorEvent event{
                    .kind = WZ_EVENT_FRAME_UPDATE,
                    .entity = record.node,
                    .other = wz::scene::INVALID_RUNTIME_ENTITY,
                    .self_is_trigger = false,
                };
                dispatch_module_event(
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

            for (const auto& collision_event :
                context.frame_storage->collision.routed_entity_events)
            {
                const auto* component =
                    behavior_for_entity(scene, collision_event.entity);
                if (!component || !component->enabled
                    || component->module.empty())
                {
                    continue;
                }

                const BehaviorEvent event{
                    .kind = collision_event_kind(collision_event.kind),
                    .entity = collision_event.entity,
                    .other = collision_event.other,
                    .self_is_trigger = collision_event.self_is_trigger,
                };
                if (event.kind == WZ_EVENT_NONE) {
                    continue;
                }

                dispatch_module_event(registry, context, *component, event);
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

            for (const auto& input_event :
                context.frame_storage->input_events.routed_entity_events)
            {
                const auto* component =
                    behavior_for_entity(scene, input_event.entity);
                if (!component || !component->enabled
                    || component->module.empty())
                {
                    continue;
                }

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
                dispatch_module_event(registry, context, *component, event);
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

            for (const auto& proximity_event :
                context.frame_storage->collision.routed_proximity_entity_events)
            {
                const auto* component =
                    behavior_for_entity(scene, proximity_event.entity);
                if (!component || !component->enabled
                    || component->module.empty())
                {
                    continue;
                }

                const BehaviorEvent event{
                    .kind = proximity_event_kind(proximity_event.kind),
                    .entity = proximity_event.entity,
                    .other = proximity_event.other,
                    .self_is_trigger = false,
                };
                if (event.kind == WZ_EVENT_NONE) {
                    continue;
                }

                dispatch_module_event(registry, context, *component, event);
            }
        }
    }

    void dispatch_behaviors(
        const wz::engine::assets::SceneInstance& scene,
        const BehaviorRegistry& registry,
        BehaviorFrameContext& context)
    {
        if (!context.commands) {
            return;
        }

        context.commands->clear();
        // Command order is deterministic: subscribed module frame.update
        // events, routed input/collision/proximity module events, then legacy
        // named functions.
        dispatch_frame_update_events_to_modules(scene, registry, context);
        dispatch_input_events_to_modules(scene, registry, context);
        dispatch_collision_events_to_modules(scene, registry, context);
        dispatch_proximity_events_to_modules(scene, registry, context);

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

            ActiveBehaviorScope active_behavior(context, component);
            registration->function(
                context,
                record.node,
                registration->user_data);
        }
    }
}
