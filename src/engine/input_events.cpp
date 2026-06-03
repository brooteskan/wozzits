#include <engine/input_events.h>

#include <algorithm>
#include <cstdint>

namespace wz::engine::input_events
{
    namespace
    {
        constexpr WzInputEventPayload invalid_payload() noexcept
        {
            return WzInputEventPayload{
                WZ_INPUT_EVENT_INVALID_VALUE,
                WZ_INPUT_EVENT_INVALID_VALUE,
                WZ_INPUT_EVENT_INVALID_VALUE,
                WZ_INPUT_EVENT_INVALID_VALUE,
                0.0f,
            };
        }

        bool listener_matches_input_event(
            const wz::engine::assets::EventListenerComponent& listener,
            const InputEvent& event) noexcept
        {
            return wz::engine::assets::listener_accepts_event(
                listener,
                event.kind);
        }
    }

    bool input_channel_matches(
        std::string_view channel,
        WzBehaviorEventKind kind) noexcept
    {
        return wz::engine::behavior::channel_mask_accepts_event(
            wz::engine::behavior::channel_mask_for_token(channel),
            kind);
    }

    void generate_input_events(
        const wz::input::InputState& input,
        std::vector<InputEvent>& out_events)
    {
        out_events.clear();

        for (uint32_t key = 0; key < 256u; ++key) {
            if (input.keyboard.pressed[key]) {
                WzInputEventPayload payload = invalid_payload();
                payload.key = key;
                out_events.push_back({
                    .kind = WZ_EVENT_INPUT_KEY_PRESSED,
                    .payload = payload,
                });
            }
            if (input.keyboard.released[key]) {
                WzInputEventPayload payload = invalid_payload();
                payload.key = key;
                out_events.push_back({
                    .kind = WZ_EVENT_INPUT_KEY_RELEASED,
                    .payload = payload,
                });
            }
        }

        for (uint32_t button = 0; button < 3u; ++button) {
            if (input.mouse.pressed[button]) {
                WzInputEventPayload payload = invalid_payload();
                payload.button = button;
                out_events.push_back({
                    .kind = WZ_EVENT_INPUT_MOUSE_BUTTON_PRESSED,
                    .payload = payload,
                });
            }
            if (input.mouse.released[button]) {
                WzInputEventPayload payload = invalid_payload();
                payload.button = button;
                out_events.push_back({
                    .kind = WZ_EVENT_INPUT_MOUSE_BUTTON_RELEASED,
                    .payload = payload,
                });
            }
        }

        const uint32_t controller_count = std::min<uint32_t>(
            input.controllers.count,
            wz::input::kMaxControllers);
        for (uint32_t controller = 0; controller < controller_count;
             ++controller)
        {
            const auto& state = input.controllers.controllers[controller];
            for (uint32_t button = 0;
                 button < wz::input::kControllerButtonCount;
                 ++button)
            {
                if (state.buttons_pressed[button]) {
                    WzInputEventPayload payload = invalid_payload();
                    payload.controller = controller;
                    payload.button = button;
                    out_events.push_back({
                        .kind = WZ_EVENT_INPUT_CONTROLLER_BUTTON_PRESSED,
                        .payload = payload,
                    });
                }
                if (state.buttons_released[button]) {
                    WzInputEventPayload payload = invalid_payload();
                    payload.controller = controller;
                    payload.button = button;
                    out_events.push_back({
                        .kind = WZ_EVENT_INPUT_CONTROLLER_BUTTON_RELEASED,
                        .payload = payload,
                    });
                }
            }

            for (uint32_t axis = 0;
                 axis < wz::input::kControllerAxisCount;
                 ++axis)
            {
                if (state.connected && state.axes_changed[axis]) {
                    WzInputEventPayload payload = invalid_payload();
                    payload.controller = controller;
                    payload.axis = axis;
                    payload.value = state.axes[axis];
                    out_events.push_back({
                        .kind = WZ_EVENT_INPUT_CONTROLLER_AXIS_CHANGED,
                        .payload = payload,
                    });
                }
            }
        }
    }

    void route_input_events(
        std::span<const InputEvent> events,
        std::span<const wz::engine::assets::SceneComponentRecord<
            wz::engine::assets::EventListenerComponent>> listeners,
        std::vector<InputEntityEvent>& out_routed_events)
    {
        out_routed_events.clear();
        out_routed_events.reserve(events.size() * listeners.size());

        for (const InputEvent& event : events) {
            for (const auto& listener : listeners) {
                if (listener_matches_input_event(listener.component, event)) {
                    out_routed_events.push_back({
                        .entity = listener.node,
                        .kind = event.kind,
                        .payload = event.payload,
                    });
                }
            }
        }
    }

    void build_input_event_frame(
        const wz::input::InputState& input,
        const wz::engine::assets::SceneInstance& scene,
        InputEventStorage& storage)
    {
        generate_input_events(input, storage.events);
        route_input_events(
            storage.events,
            scene.event_listeners,
            storage.routed_entity_events);
    }
}
