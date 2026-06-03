#include "behavior_test_support.h"

namespace
{
    struct MultiModuleProbe
    {
        uint32_t a_calls = 0;
        uint32_t b_calls = 0;
    };

    void count_module_a(
        BehaviorFrameContext&,
        const BehaviorEvent& event,
        void* user)
    {
        auto* probe = static_cast<MultiModuleProbe*>(user);
        ASSERT_NE(probe, nullptr);
        if (event.kind == WZ_EVENT_COLLISION_ENTER) {
            ++probe->a_calls;
        }
    }

    void count_module_b(
        BehaviorFrameContext&,
        const BehaviorEvent& event,
        void* user)
    {
        auto* probe = static_cast<MultiModuleProbe*>(user);
        ASSERT_NE(probe, nullptr);
        if (event.kind == WZ_EVENT_COLLISION_ENTER) {
            ++probe->b_calls;
        }
    }

    ModuleEventProbe* g_desc_default_probe = nullptr;

    void desc_default_event_handler(
        const WzBehaviorFrameFacts*,
        const WzBehaviorEvent* event,
        void* user)
    {
        auto* probe = static_cast<ModuleEventProbe*>(user);
        ASSERT_NE(probe, nullptr);
        ASSERT_NE(event, nullptr);
        ++probe->calls;
        probe->kinds.push_back(event->kind);
        probe->entities.push_back(event->entity);
    }

    uint8_t register_desc_default_pack(WzBehaviorPluginApi* api)
    {
        if (!api || !api->register_module_desc || !g_desc_default_probe) {
            return 0;
        }
        static const char* channels[] = { "collision.enter" };
        const WzBehaviorModuleDesc desc{
            .size = sizeof(WzBehaviorModuleDesc),
            .module = "desc_default",
            .on_event = desc_default_event_handler,
            .event_channels = channels,
            .event_channel_count = 1u,
            .module_user_data = g_desc_default_probe,
        };
        return api->register_module_desc(api->user, &desc);
    }
}

TEST(BehaviorDispatch, DispatchesRoutedCollisionEventsToRegisteredModule)
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
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
            .self_is_trigger = true,
        },
    };

    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.calls, 2u);
    ASSERT_EQ(probe.kinds.size(), 2u);
    EXPECT_EQ(probe.kinds[0], WZ_EVENT_FRAME_UPDATE);
    EXPECT_EQ(probe.kinds[1], WZ_EVENT_COLLISION_ENTER);
    EXPECT_EQ(probe.last_kind, WZ_EVENT_COLLISION_ENTER);
    EXPECT_EQ(probe.last_entity, 4u);
    EXPECT_EQ(probe.last_other, 9u);
    EXPECT_EQ(probe.last_trigger, 1u);
    EXPECT_TRUE(probe.wrote_command);
    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u);
    EXPECT_EQ(frame_storage.behavior_commands.commands[0].entity, 4u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[0].kind,
        BehaviorCommandKind::AddLocalTranslation);

    g_module_event_probe = nullptr;
}

TEST(BehaviorDispatch, DispatchesRoutedProximityEventsToRegisteredModule)
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

    ASSERT_EQ(probe.calls, 2u);
    ASSERT_EQ(probe.kinds.size(), 2u);
    EXPECT_EQ(probe.kinds[0], WZ_EVENT_FRAME_UPDATE);
    EXPECT_EQ(probe.kinds[1], WZ_EVENT_PROXIMITY_ENTER);
    EXPECT_EQ(probe.last_kind, WZ_EVENT_PROXIMITY_ENTER);
    EXPECT_EQ(probe.last_entity, 4u);
    EXPECT_EQ(probe.last_other, 9u);
    EXPECT_FALSE(probe.wrote_command)
        << "test handler only writes on collision.enter";

    g_module_event_probe = nullptr;
}

TEST(BehaviorDispatch, DispatchesCollisionBeforeProximityInSameFrame)
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

    ASSERT_EQ(probe.kinds.size(), 3u);
    EXPECT_EQ(probe.kinds[0], WZ_EVENT_FRAME_UPDATE);
    EXPECT_EQ(probe.kinds[1], WZ_EVENT_COLLISION_ENTER);
    EXPECT_EQ(probe.kinds[2], WZ_EVENT_PROXIMITY_ENTER);
    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[0].kind,
        BehaviorCommandKind::AddLocalTranslation);

    g_module_event_probe = nullptr;
}

TEST(BehaviorDispatch, DispatchesFrameUpdateToRegisteredModule)
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

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.calls, 1u);
    EXPECT_EQ(probe.last_kind, WZ_EVENT_FRAME_UPDATE);
    EXPECT_EQ(probe.last_entity, 4u);
    EXPECT_EQ(probe.last_other, WZ_INVALID_BEHAVIOR_ENTITY);
    EXPECT_EQ(probe.last_trigger, 0u);
    EXPECT_FALSE(probe.wrote_command);
    EXPECT_TRUE(frame_storage.behavior_commands.commands.empty());

    g_module_event_probe = nullptr;
}

TEST(BehaviorDispatch, SkipsFrameUpdateWithoutBehaviorSubscription)
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
    ASSERT_EQ(scene.behaviors.size(), 1u);
    scene.behaviors[0].component.events.clear();
    scene.behaviors[0].component.channel_mask = 0u;

    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(probe.calls, 0u);
    EXPECT_TRUE(frame_storage.behavior_commands.commands.empty());

    g_module_event_probe = nullptr;
}

TEST(BehaviorDispatch, FrameUpdateRepeatsAndCollisionEventsAreFrameLocal)
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

    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.kinds.size(), 2u);
    EXPECT_EQ(probe.kinds[0], WZ_EVENT_FRAME_UPDATE);
    EXPECT_EQ(probe.kinds[1], WZ_EVENT_COLLISION_ENTER);
    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u);

    frame_storage.collision.routed_entity_events.clear();
    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.kinds.size(), 3u);
    EXPECT_EQ(probe.kinds[2], WZ_EVENT_FRAME_UPDATE);
    EXPECT_TRUE(frame_storage.behavior_commands.commands.empty())
        << "dispatch clears prior-frame collision commands";

    g_module_event_probe = nullptr;
}

TEST(BehaviorDispatch, ModuleAndLegacyBehaviorComposeOnSameEntity)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ModuleEventProbe probe{};
    CallCounter counter{};
    g_module_event_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_module_event_pack));
    registry.register_behavior("module_test", "count", count_behavior, &counter);

    SceneInstance scene = scene_with_behavior(
        4u,
        "module_test",
        "count");
    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.kinds.size(), 2u);
    EXPECT_EQ(probe.kinds[0], WZ_EVENT_FRAME_UPDATE);
    EXPECT_EQ(probe.kinds[1], WZ_EVENT_COLLISION_ENTER);
    EXPECT_EQ(counter.calls, 1u);
    EXPECT_EQ(counter.last_entity, 4u);
    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 2u);
    EXPECT_EQ(frame_storage.behavior_commands.commands[0].entity, 4u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[0].kind,
        BehaviorCommandKind::AddLocalTranslation);
    EXPECT_FLOAT_EQ(
        frame_storage.behavior_commands.commands[0].values[1],
        3.0f);
    EXPECT_EQ(frame_storage.behavior_commands.commands[1].entity, 4u);
    EXPECT_FLOAT_EQ(
        frame_storage.behavior_commands.commands[1].values[0],
        1.0f);
    EXPECT_FLOAT_EQ(
        frame_storage.behavior_commands.commands[1].values[1],
        2.0f);
    EXPECT_FLOAT_EQ(
        frame_storage.behavior_commands.commands[1].values[2],
        3.0f);

    g_module_event_probe = nullptr;
}

TEST(BehaviorDispatch, MultipleBehaviorBindingsOnSameEntityReceiveSameEvent)
{
    BehaviorRegistry registry;
    MultiModuleProbe probe{};
    const auto mask =
        wz::engine::behavior::channel_mask_for_token("collision.enter");
    ASSERT_TRUE(registry.register_module(
        "module_a",
        count_module_a,
        &probe).valid());
    ASSERT_TRUE(registry.register_module(
        "module_b",
        count_module_b,
        &probe).valid());

    SceneInstance scene{};
    scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
        .node = 4u,
        .component = BehaviorComponent{
            .module = "module_a",
            .enabled = true,
            .events = { "collision.enter" },
            .channel_mask = mask,
        },
    });
    scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
        .node = 4u,
        .component = BehaviorComponent{
            .module = "module_b",
            .enabled = true,
            .events = { "collision.enter" },
            .channel_mask = mask,
        },
    });

    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(probe.a_calls, 1u);
    EXPECT_EQ(probe.b_calls, 1u);
}

TEST(BehaviorDispatch, SkipsBindingsThatDoNotAcceptEventKind)
{
    BehaviorRegistry registry;
    MultiModuleProbe probe{};
    ASSERT_TRUE(registry.register_module(
        "collision_module",
        count_module_a,
        &probe).valid());
    ASSERT_TRUE(registry.register_module(
        "input_module",
        count_module_b,
        &probe).valid());

    SceneInstance scene{};
    scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
        .node = 4u,
        .component = BehaviorComponent{
            .module = "collision_module",
            .enabled = true,
            .events = { "collision.*" },
            .channel_mask =
                wz::engine::behavior::channel_mask_for_token("collision.*"),
        },
    });
    scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
        .node = 4u,
        .component = BehaviorComponent{
            .module = "input_module",
            .enabled = true,
            .events = { "input.*" },
            .channel_mask =
                wz::engine::behavior::channel_mask_for_token("input.*"),
        },
    });

    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(probe.a_calls, 1u);
    EXPECT_EQ(probe.b_calls, 0u);
}

TEST(BehaviorDispatch, UsesPluginDescriptorDefaultEventsWhenBindingOmitsEvents)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ModuleEventProbe probe{};
    g_desc_default_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_desc_default_pack));

    SceneInstance scene{};
    scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
        .node = 4u,
        .component = BehaviorComponent{
            .module = "desc_default",
            .enabled = true,
        },
    });

    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Exit,
        },
    };
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.kinds.size(), 1u);
    EXPECT_EQ(probe.kinds[0], WZ_EVENT_COLLISION_ENTER);
    EXPECT_EQ(probe.entities[0], 4u);
    g_desc_default_probe = nullptr;
}

