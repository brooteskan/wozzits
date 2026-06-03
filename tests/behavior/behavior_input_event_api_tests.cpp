#include "behavior_test_support.h"

#include <string>
#include <vector>

namespace
{
    struct InputEventApiProbe
    {
        uint32_t calls = 0;
        std::vector<WzBehaviorEventKind> kinds;
        uint32_t frame_key = 0u;
        uint32_t key = 0u;
        uint32_t mouse_key = 0u;
        uint32_t mouse_button = 0u;
        uint32_t controller = 0u;
        uint32_t controller_button = 0u;
        uint32_t collision_key = 0u;
        uint8_t snapshot_w_down_during_input = 0u;
    };

    InputEventApiProbe* g_input_event_api_probe = nullptr;

    void input_event_api_handler(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void* user)
    {
        auto* probe = static_cast<InputEventApiProbe*>(user);
        ASSERT_NE(probe, nullptr);
        ASSERT_NE(facts, nullptr);
        ASSERT_NE(event, nullptr);

        ++probe->calls;
        probe->kinds.push_back(event->kind);

        if (wz_is_event(event, WZ_EVENT_FRAME_UPDATE)) {
            probe->frame_key = wz_input_event_key(facts);
        }
        else if (wz_is_event(event, WZ_EVENT_INPUT_KEY_PRESSED)) {
            probe->key = wz_input_event_key(facts);
            probe->snapshot_w_down_during_input =
                wz_key_down(facts, WZ_KEY_W);
        }
        else if (wz_is_event(event, WZ_EVENT_INPUT_MOUSE_BUTTON_PRESSED)) {
            probe->mouse_key = wz_input_event_key(facts);
            probe->mouse_button = wz_input_event_mouse_button(facts);
        }
        else if (
            wz_is_event(event, WZ_EVENT_INPUT_CONTROLLER_BUTTON_PRESSED))
        {
            probe->controller = wz_input_event_controller(facts);
            probe->controller_button =
                wz_input_event_controller_button(facts);
        }
        else if (wz_is_event(event, WZ_EVENT_COLLISION_ENTER)) {
            probe->collision_key = wz_input_event_key(facts);
        }
    }

    uint8_t register_input_event_api_pack(WzBehaviorPluginApi* api)
    {
        if (!api || !api->register_module || !g_input_event_api_probe) {
            return 0;
        }

        return api->register_module(
            api->user,
            "input_event_api_test",
            input_event_api_handler,
            g_input_event_api_probe);
    }
}

TEST(BehaviorInputEventApi, EventNamesIncludeInputEvents)
{
    EXPECT_EQ(
        std::string(wz_event_name(WZ_EVENT_INPUT_KEY_PRESSED)),
        "input.key.pressed");
    EXPECT_EQ(
        std::string(wz_event_name(WZ_EVENT_INPUT_KEY_RELEASED)),
        "input.key.released");
    EXPECT_EQ(
        std::string(wz_event_name(WZ_EVENT_INPUT_MOUSE_BUTTON_PRESSED)),
        "input.mouse_button.pressed");
    EXPECT_EQ(
        std::string(wz_event_name(WZ_EVENT_INPUT_MOUSE_BUTTON_RELEASED)),
        "input.mouse_button.released");
    EXPECT_EQ(
        std::string(wz_event_name(WZ_EVENT_INPUT_CONTROLLER_BUTTON_PRESSED)),
        "input.controller_button.pressed");
    EXPECT_EQ(
        std::string(wz_event_name(WZ_EVENT_INPUT_CONTROLLER_BUTTON_RELEASED)),
        "input.controller_button.released");
}

TEST(BehaviorInputEventApi, ActivePayloadHelpersAreDispatchScoped)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    InputEventApiProbe probe{};
    g_input_event_api_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_input_event_api_pack));

    SceneInstance scene = scene_with_behavior(
        4u,
        "input_event_api_test",
        "");
    wz::engine::FrameContext frame_context{};
    frame_context.input.keyboard.down[WZ_KEY_W] = true;

    wz::engine::FrameStorage frame_storage{};
    frame_storage.input_events.routed_entity_events = {
        wz::engine::input_events::InputEntityEvent{
            .entity = 4u,
            .kind = WZ_EVENT_INPUT_KEY_PRESSED,
            .payload = {
                WZ_KEY_SPACE,
                WZ_INPUT_EVENT_INVALID_VALUE,
                WZ_INPUT_EVENT_INVALID_VALUE,
            },
        },
        wz::engine::input_events::InputEntityEvent{
            .entity = 4u,
            .kind = WZ_EVENT_INPUT_MOUSE_BUTTON_PRESSED,
            .payload = {
                WZ_INPUT_EVENT_INVALID_VALUE,
                WZ_INPUT_EVENT_INVALID_VALUE,
                WZ_MOUSE_BUTTON_RIGHT,
            },
        },
        wz::engine::input_events::InputEntityEvent{
            .entity = 4u,
            .kind = WZ_EVENT_INPUT_CONTROLLER_BUTTON_PRESSED,
            .payload = {
                WZ_INPUT_EVENT_INVALID_VALUE,
                1u,
                WZ_CONTROLLER_BUTTON_A,
            },
        },
    };
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };

    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.calls, 5u);
    ASSERT_EQ(probe.kinds.size(), 5u);
    EXPECT_EQ(probe.kinds[0], WZ_EVENT_FRAME_UPDATE);
    EXPECT_EQ(probe.kinds[1], WZ_EVENT_INPUT_KEY_PRESSED);
    EXPECT_EQ(probe.kinds[2], WZ_EVENT_INPUT_MOUSE_BUTTON_PRESSED);
    EXPECT_EQ(probe.kinds[3], WZ_EVENT_INPUT_CONTROLLER_BUTTON_PRESSED);
    EXPECT_EQ(probe.kinds[4], WZ_EVENT_COLLISION_ENTER);
    EXPECT_EQ(probe.frame_key, WZ_INPUT_EVENT_INVALID_VALUE);
    EXPECT_EQ(probe.key, WZ_KEY_SPACE);
    EXPECT_EQ(probe.snapshot_w_down_during_input, 1u);
    EXPECT_EQ(probe.mouse_key, WZ_INPUT_EVENT_INVALID_VALUE);
    EXPECT_EQ(probe.mouse_button, WZ_MOUSE_BUTTON_RIGHT);
    EXPECT_EQ(probe.controller, 1u);
    EXPECT_EQ(probe.controller_button, WZ_CONTROLLER_BUTTON_A);
    EXPECT_EQ(probe.collision_key, WZ_INPUT_EVENT_INVALID_VALUE);

    g_input_event_api_probe = nullptr;
}
