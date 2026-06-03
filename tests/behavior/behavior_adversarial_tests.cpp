#include "behavior_test_support.h"

TEST(Adversarial, DisabledBehaviorReceivesNoModuleEvents)
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
        "",
        false);
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

    EXPECT_EQ(probe.calls, 0u)
        << "disabled behavior component must not receive frame_update or "
           "collision events through the module path";
    EXPECT_TRUE(frame_storage.behavior_commands.commands.empty());

    g_module_event_probe = nullptr;
}

TEST(Adversarial, EmptyModuleNameSkipsModuleDispatch)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ModuleEventProbe probe{};
    g_module_event_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_module_event_pack));

    SceneInstance scene = scene_with_behavior(4u, "", "some_name");
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

    EXPECT_EQ(probe.calls, 0u)
        << "empty module name must skip both frame_update and collision "
           "module dispatch";

    g_module_event_probe = nullptr;
}

TEST(Adversarial, CollisionEventForEntityWithNoBehaviorComponentIsIgnored)
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
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 99u,
            .other = 4u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
    };
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.kinds.size(), 1u);
    EXPECT_EQ(probe.kinds[0], WZ_EVENT_FRAME_UPDATE)
        << "only frame_update for entity 4; collision for entity 99 (no "
           "behavior component) must be ignored";

    g_module_event_probe = nullptr;
}

TEST(Adversarial, DispatchWithNoRegisteredModulesOrBehaviors)
{
    BehaviorRegistry registry;
    SceneInstance scene = scene_with_behavior(
        4u,
        "nonexistent_module",
        "nonexistent_behavior");
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

    EXPECT_TRUE(frame_storage.behavior_commands.commands.empty())
        << "dispatch with empty registry must produce no commands";
}

TEST(Adversarial, DispatchWithNullFrameStorageSkipsCollisionButRunsUpdate)
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
        .frame_context = nullptr,
        .frame_storage = nullptr,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.kinds.size(), 1u);
    EXPECT_EQ(probe.kinds[0], WZ_EVENT_FRAME_UPDATE)
        << "frame_update should fire even with null frame_storage; "
           "collision dispatch should be skipped";

    g_module_event_probe = nullptr;
}

TEST(Adversarial, DispatchWithNullCommandBufferReturnsEarly)
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
    BehaviorFrameContext context{
        .scene = &scene,
        .commands = nullptr,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(probe.calls, 0u)
        << "null command buffer must cause early return before any dispatch";

    g_module_event_probe = nullptr;
}

TEST(Adversarial, EmptySceneDispatchesNothing)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ModuleEventProbe probe{};
    g_module_event_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_module_event_pack));

    SceneInstance scene{};
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

TEST(Adversarial, MultipleEntitiesSameModuleDifferentCollisionKinds)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ModuleEventProbe probe{};
    g_module_event_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_module_event_pack));

    SceneInstance scene{};
    scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
        .node = 4u,
        .component = BehaviorComponent{
            .module = "module_test",
            .enabled = true,
        },
    });
    scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
        .node = 5u,
        .component = BehaviorComponent{
            .module = "module_test",
            .enabled = true,
        },
    });
    subscribe_frame_update(scene, 4u);
    subscribe_frame_update(scene, 5u);

    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 5u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
        wz::engine::collision::CollisionEntityEvent{
            .entity = 5u,
            .other = 4u,
            .kind = wz::engine::collision::CollisionEventKind::Exit,
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
    EXPECT_EQ(probe.entities[0], 4u);
    EXPECT_EQ(probe.kinds[1], WZ_EVENT_FRAME_UPDATE);
    EXPECT_EQ(probe.entities[1], 5u);
    EXPECT_EQ(probe.kinds[2], WZ_EVENT_COLLISION_ENTER);
    EXPECT_EQ(probe.entities[2], 4u);
    EXPECT_EQ(probe.others[2], 5u);
    EXPECT_EQ(probe.kinds[3], WZ_EVENT_COLLISION_EXIT);
    EXPECT_EQ(probe.entities[3], 5u);
    EXPECT_EQ(probe.others[3], 4u);
    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u)
        << "only collision.enter writes a command; exit does not";
    EXPECT_EQ(frame_storage.behavior_commands.commands[0].entity, 4u);

    g_module_event_probe = nullptr;
}

TEST(Adversarial, ApplyCommandsWithInvalidRuntimeEntityId)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "adversarial_invalid_entity";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    BehaviorCommandBuffer commands{};
    commands.add_local_translation(
        wz::scene::INVALID_RUNTIME_ENTITY, 1.0f, 2.0f, 3.0f);
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 0u);
    EXPECT_TRUE(changed.empty());
}

TEST(Adversarial, ApplyEmptyCommandBufferIsNoOp)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "adversarial_empty_commands";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.local.translation[0] = 5.0f;
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    BehaviorCommandBuffer commands{};
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 0u);
    EXPECT_TRUE(changed.empty());

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.local.m[12], 5.0f)
        << "empty command buffer must not touch transforms";
}

TEST(Adversarial, ApplyCommandsWithNoneKindIgnored)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "adversarial_none_kind";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    BehaviorCommandBuffer commands{};
    commands.commands.push_back(BehaviorCommand{
        .entity = actor,
        .kind = BehaviorCommandKind::None,
        .values = { 99.0f, 99.0f, 99.0f, 0.0f },
    });
    std::vector<RuntimeEntityId> changed;

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        &changed);

    EXPECT_EQ(applied, 0u);
    EXPECT_TRUE(changed.empty());
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.local.m[12], 0.0f)
        << "BehaviorCommandKind::None must not mutate transforms";
}

TEST(Adversarial, ApplyCommandsWithNullChangedEntitiesVector)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "adversarial_null_changed";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId actor =
        result.instance.authored_to_runtime["actor"];
    BehaviorCommandBuffer commands{};
    commands.add_local_translation(actor, 1.0f, 2.0f, 3.0f);

    const uint32_t applied = apply_behavior_commands(
        result.instance,
        commands.commands,
        nullptr);

    EXPECT_EQ(applied, 1u);
    const auto& actor_node = wz::core::graph::node_data(
        result.instance.storage.polytree,
        actor);
    EXPECT_FLOAT_EQ(actor_node.local.m[12], 1.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[13], 2.0f);
    EXPECT_FLOAT_EQ(actor_node.local.m[14], 3.0f);
}

TEST(Adversarial, RegisterModuleWithEmptyNameFails)
{
    BehaviorRegistry registry;

    const BehaviorModuleHandle handle = registry.register_module(
        "",
        [](BehaviorFrameContext&, const BehaviorEvent&, void*) {},
        nullptr);

    EXPECT_FALSE(handle.valid());
    EXPECT_TRUE(registry.modules().empty());
}

TEST(Adversarial, RegisterModuleWithNullFunctionFails)
{
    BehaviorRegistry registry;

    const BehaviorModuleHandle handle = registry.register_module(
        "test_module",
        nullptr,
        nullptr);

    EXPECT_FALSE(handle.valid());
    EXPECT_TRUE(registry.modules().empty());
}

TEST(Adversarial, ReRegisterModuleReplacesHandler)
{
    BehaviorRegistry registry;
    uint32_t call_count_a = 0;
    uint32_t call_count_b = 0;

    auto handler_a = [](BehaviorFrameContext&,
                        const BehaviorEvent&,
                        void* user)
    {
        ++(*static_cast<uint32_t*>(user));
    };
    auto handler_b = [](BehaviorFrameContext&,
                        const BehaviorEvent&,
                        void* user)
    {
        ++(*static_cast<uint32_t*>(user));
    };

    const BehaviorModuleHandle a = registry.register_module(
        "test_module",
        handler_a,
        &call_count_a);
    const BehaviorModuleHandle b = registry.register_module(
        "test_module",
        handler_b,
        &call_count_b);

    EXPECT_EQ(a.index, b.index);
    ASSERT_EQ(registry.modules().size(), 1u);

    const BehaviorModuleRegistration* reg = registry.get_module(a);
    ASSERT_NE(reg, nullptr);
    EXPECT_EQ(reg->on_event, handler_b);
    EXPECT_EQ(reg->user_data, &call_count_b);
}

TEST(Adversarial, FindModuleInvalidHandleReturnsNull)
{
    BehaviorRegistry registry;

    BehaviorModuleHandle bad_handle{ .index = 42u };
    EXPECT_EQ(registry.get_module(bad_handle), nullptr);

    BehaviorModuleHandle invalid_handle{};
    EXPECT_EQ(registry.get_module(invalid_handle), nullptr);

    EXPECT_FALSE(registry.find_module("nonexistent").has_value());
}

TEST(Adversarial, CollisionStayAndExitRouteCorrectEventKinds)
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
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Stay,
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

    ASSERT_EQ(probe.kinds.size(), 3u);
    EXPECT_EQ(probe.kinds[0], WZ_EVENT_FRAME_UPDATE);
    EXPECT_EQ(probe.kinds[1], WZ_EVENT_COLLISION_STAY);
    EXPECT_EQ(probe.kinds[2], WZ_EVENT_COLLISION_EXIT);
    EXPECT_TRUE(frame_storage.behavior_commands.commands.empty())
        << "stay and exit events should not write commands in the test handler";

    g_module_event_probe = nullptr;
}

