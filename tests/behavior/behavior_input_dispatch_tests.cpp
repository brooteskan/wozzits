#include "behavior_test_support.h"

TEST(BehaviorInputDispatch, DispatchesInputBeforeCollisionAndProximity)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ModuleEventProbe probe{};
    g_module_event_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_module_event_pack));

    SceneInstance scene = scene_with_behavior(
        4u,
        "module_test",
        "ignored_function_name");

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
    };
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    frame_storage.collision.routed_proximity_entity_events = {
        wz::engine::collision::ProximityEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::ProximityEventKind::Enter,
        },
    };

    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.kinds.size(), 4u);
    EXPECT_EQ(probe.kinds[0], WZ_EVENT_FRAME_UPDATE);
    EXPECT_EQ(probe.kinds[1], WZ_EVENT_INPUT_KEY_PRESSED);
    EXPECT_EQ(probe.kinds[2], WZ_EVENT_COLLISION_ENTER);
    EXPECT_EQ(probe.kinds[3], WZ_EVENT_PROXIMITY_ENTER);
    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[0].kind,
        BehaviorCommandKind::AddLocalTranslation);

    g_module_event_probe = nullptr;
}

TEST(BehaviorInputDispatch, InputEventsAreFrameLocal)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ModuleEventProbe probe{};
    g_module_event_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_module_event_pack));

    SceneInstance scene = scene_with_behavior(
        4u,
        "module_test",
        "");
    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

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
    };
    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.kinds.size(), 2u);
    EXPECT_EQ(probe.kinds[0], WZ_EVENT_FRAME_UPDATE);
    EXPECT_EQ(probe.kinds[1], WZ_EVENT_INPUT_KEY_PRESSED);

    frame_storage.input_events.routed_entity_events.clear();
    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.kinds.size(), 3u);
    EXPECT_EQ(probe.kinds[2], WZ_EVENT_FRAME_UPDATE);

    g_module_event_probe = nullptr;
}
