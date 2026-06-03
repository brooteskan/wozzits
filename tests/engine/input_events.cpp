#include <gtest/gtest.h>

#include <engine/input_events.h>

#include <algorithm>

namespace
{
    wz::engine::assets::SceneComponentRecord<
        wz::engine::assets::EventListenerComponent>
    listener(
        wz::scene::RuntimeEntityId entity,
        std::vector<std::string> channels)
    {
        const auto compiled =
            wz::engine::behavior::compile_channel_mask(channels);
        return {
            .node = entity,
            .component = {
                .channels = std::move(channels),
                .channel_mask = compiled.mask,
            },
        };
    }
}

TEST(InputEvents, ChannelMatchingUsesExactInputTokens)
{
    using namespace wz::engine::input_events;

    EXPECT_TRUE(input_channel_matches(
        "input.key.pressed",
        WZ_EVENT_INPUT_KEY_PRESSED));
    EXPECT_FALSE(input_channel_matches(
        "input.key.pressed",
        WZ_EVENT_INPUT_KEY_RELEASED));
    EXPECT_TRUE(input_channel_matches(
        "input.*",
        WZ_EVENT_INPUT_KEY_PRESSED));
    EXPECT_TRUE(input_channel_matches(
        "input.*",
        WZ_EVENT_INPUT_MOUSE_BUTTON_PRESSED));
    EXPECT_FALSE(input_channel_matches(
        "collision.enter",
        WZ_EVENT_INPUT_KEY_PRESSED));
    EXPECT_FALSE(input_channel_matches(
        "input.key.pressed",
        WZ_EVENT_COLLISION_ENTER));
}

TEST(InputEvents, GenerateInputEventsFromPressedAndReleasedEdges)
{
    using namespace wz::engine::input_events;

    wz::input::InputState input{};
    input.keyboard.pressed[WZ_KEY_SPACE] = true;
    input.keyboard.released[WZ_KEY_ESCAPE] = true;
    input.mouse.pressed[WZ_MOUSE_BUTTON_LEFT] = true;
    input.mouse.released[WZ_MOUSE_BUTTON_RIGHT] = true;
    input.controllers.count = 1;
    input.controllers.controllers[0].buttons_pressed[WZ_CONTROLLER_BUTTON_A] =
        true;
    input.controllers.controllers[0].buttons_released[WZ_CONTROLLER_BUTTON_B] =
        true;
    input.controllers.controllers[0].connected = true;
    input.controllers.controllers[0].axes[WZ_CONTROLLER_AXIS_LEFT_X] = 0.5f;
    input.controllers.controllers[0].axes_changed
        [WZ_CONTROLLER_AXIS_LEFT_X] = true;

    std::vector<InputEvent> events;
    generate_input_events(input, events);

    const auto has_event =
        [&events](
            WzBehaviorEventKind kind,
            uint32_t key,
            uint32_t controller,
            uint32_t button,
            uint32_t axis = WZ_INPUT_EVENT_INVALID_VALUE,
            float value = 0.0f)
        {
            return std::any_of(
                events.begin(),
                events.end(),
                [&](const InputEvent& event)
                {
                    return event.kind == kind
                        && event.payload.key == key
                        && event.payload.controller == controller
                        && event.payload.button == button
                        && event.payload.axis == axis
                        && event.payload.value == value;
                });
        };

    ASSERT_EQ(events.size(), 7u);
    EXPECT_TRUE(has_event(
        WZ_EVENT_INPUT_KEY_PRESSED,
        WZ_KEY_SPACE,
        WZ_INPUT_EVENT_INVALID_VALUE,
        WZ_INPUT_EVENT_INVALID_VALUE));
    EXPECT_TRUE(has_event(
        WZ_EVENT_INPUT_KEY_RELEASED,
        WZ_KEY_ESCAPE,
        WZ_INPUT_EVENT_INVALID_VALUE,
        WZ_INPUT_EVENT_INVALID_VALUE));
    EXPECT_TRUE(has_event(
        WZ_EVENT_INPUT_MOUSE_BUTTON_PRESSED,
        WZ_INPUT_EVENT_INVALID_VALUE,
        WZ_INPUT_EVENT_INVALID_VALUE,
        WZ_MOUSE_BUTTON_LEFT));
    EXPECT_TRUE(has_event(
        WZ_EVENT_INPUT_MOUSE_BUTTON_RELEASED,
        WZ_INPUT_EVENT_INVALID_VALUE,
        WZ_INPUT_EVENT_INVALID_VALUE,
        WZ_MOUSE_BUTTON_RIGHT));
    EXPECT_TRUE(has_event(
        WZ_EVENT_INPUT_CONTROLLER_BUTTON_PRESSED,
        WZ_INPUT_EVENT_INVALID_VALUE,
        0u,
        WZ_CONTROLLER_BUTTON_A));
    EXPECT_TRUE(has_event(
        WZ_EVENT_INPUT_CONTROLLER_BUTTON_RELEASED,
        WZ_INPUT_EVENT_INVALID_VALUE,
        0u,
        WZ_CONTROLLER_BUTTON_B));
    EXPECT_TRUE(has_event(
        WZ_EVENT_INPUT_CONTROLLER_AXIS_CHANGED,
        WZ_INPUT_EVENT_INVALID_VALUE,
        0u,
        WZ_INPUT_EVENT_INVALID_VALUE,
        WZ_CONTROLLER_AXIS_LEFT_X,
        0.5f));
}

TEST(InputEvents, RouteInputEventsRequiresListener)
{
    using namespace wz::engine::input_events;

    std::vector<InputEvent> events{
        InputEvent{
            .kind = WZ_EVENT_INPUT_KEY_PRESSED,
            .payload = {
                WZ_KEY_SPACE,
                WZ_INPUT_EVENT_INVALID_VALUE,
                WZ_INPUT_EVENT_INVALID_VALUE,
                WZ_INPUT_EVENT_INVALID_VALUE,
                0.0f,
            },
        },
    };
    std::vector<wz::engine::assets::SceneComponentRecord<
        wz::engine::assets::EventListenerComponent>> listeners;
    std::vector<InputEntityEvent> routed;

    route_input_events(events, listeners, routed);

    EXPECT_TRUE(routed.empty());
}

TEST(InputEvents, RouteInputEventsFiltersByChannel)
{
    using namespace wz::engine::input_events;

    std::vector<InputEvent> events{
        InputEvent{
            .kind = WZ_EVENT_INPUT_KEY_PRESSED,
            .payload = {
                WZ_KEY_SPACE,
                WZ_INPUT_EVENT_INVALID_VALUE,
                WZ_INPUT_EVENT_INVALID_VALUE,
                WZ_INPUT_EVENT_INVALID_VALUE,
                0.0f,
            },
        },
        InputEvent{
            .kind = WZ_EVENT_INPUT_MOUSE_BUTTON_PRESSED,
            .payload = {
                WZ_INPUT_EVENT_INVALID_VALUE,
                WZ_INPUT_EVENT_INVALID_VALUE,
                WZ_MOUSE_BUTTON_LEFT,
                WZ_INPUT_EVENT_INVALID_VALUE,
                0.0f,
            },
        },
    };
    std::vector listeners{
        listener(1u, { "input.key.pressed" }),
        listener(2u, { "input.*" }),
        listener(3u, { "collision.enter" }),
    };
    std::vector<InputEntityEvent> routed;

    route_input_events(events, listeners, routed);

    ASSERT_EQ(routed.size(), 3u);
    EXPECT_EQ(routed[0].entity, 1u);
    EXPECT_EQ(routed[0].kind, WZ_EVENT_INPUT_KEY_PRESSED);
    EXPECT_EQ(routed[1].entity, 2u);
    EXPECT_EQ(routed[1].kind, WZ_EVENT_INPUT_KEY_PRESSED);
    EXPECT_EQ(routed[2].entity, 2u);
    EXPECT_EQ(routed[2].kind, WZ_EVENT_INPUT_MOUSE_BUTTON_PRESSED);
}

TEST(InputEvents, BuildInputEventFrameRoutesSceneListeners)
{
    using namespace wz::engine::input_events;

    wz::input::InputState input{};
    input.keyboard.pressed[WZ_KEY_W] = true;

    wz::engine::assets::SceneInstance scene{};
    scene.event_listeners = {
        listener(4u, { "input.key.pressed" }),
        listener(5u, { "input.mouse_button.pressed" }),
    };

    InputEventStorage storage{};
    build_input_event_frame(input, scene, storage);

    ASSERT_EQ(storage.events.size(), 1u);
    ASSERT_EQ(storage.routed_entity_events.size(), 1u);
    EXPECT_EQ(storage.routed_entity_events[0].entity, 4u);
    EXPECT_EQ(storage.routed_entity_events[0].payload.key, WZ_KEY_W);
}
