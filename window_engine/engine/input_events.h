#pragma once

#include <engine/assets/scene/scene_instance.h>
#include <engine/behavior/behavior_plugin_abi.h>
#include <input/input.h>
#include <scene/scene_ecs.h>

#include <span>
#include <string_view>
#include <vector>

namespace wz::engine::input_events
{
    struct InputEvent
    {
        WzBehaviorEventKind kind = WZ_EVENT_NONE;
        WzInputEventPayload payload{
            WZ_INPUT_EVENT_INVALID_VALUE,
            WZ_INPUT_EVENT_INVALID_VALUE,
            WZ_INPUT_EVENT_INVALID_VALUE,
        };
    };

    struct InputEntityEvent
    {
        wz::scene::RuntimeEntityId entity =
            wz::scene::INVALID_RUNTIME_ENTITY;
        WzBehaviorEventKind kind = WZ_EVENT_NONE;
        WzInputEventPayload payload{
            WZ_INPUT_EVENT_INVALID_VALUE,
            WZ_INPUT_EVENT_INVALID_VALUE,
            WZ_INPUT_EVENT_INVALID_VALUE,
        };
    };

    struct InputEventStorage
    {
        std::vector<InputEvent> events;
        std::vector<InputEntityEvent> routed_entity_events;
    };

    bool input_channel_matches(
        std::string_view channel,
        WzBehaviorEventKind kind) noexcept;

    void generate_input_events(
        const wz::input::InputState& input,
        std::vector<InputEvent>& out_events);

    void route_input_events(
        std::span<const InputEvent> events,
        std::span<const wz::engine::assets::SceneComponentRecord<
            wz::engine::assets::EventListenerComponent>> listeners,
        std::vector<InputEntityEvent>& out_routed_events);

    void build_input_event_frame(
        const wz::input::InputState& input,
        const wz::engine::assets::SceneInstance& scene,
        InputEventStorage& storage);
}
