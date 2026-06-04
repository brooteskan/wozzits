#include "behavior_test_support.h"

#include <engine/behavior/behavior_plugin_adapter.h>
#include <logging/internal/memory_log_sink.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{
    struct TestState
    {
        uint32_t init_count = 0;
        uint32_t event_count = 0;
        uint32_t sentinel = 0;
        WzBehaviorEntityId entity = WZ_INVALID_BEHAVIOR_ENTITY;
    };

    struct InitProbe
    {
        std::vector<WzBehaviorEntityId> init_order;
    };

    struct SharedGroupState
    {
        uint32_t coordinator_inits = 0;
        uint32_t participant_inits = 0;
        uint32_t participant_events = 0;
        uint32_t sentinel = 0;
        WzBehaviorEntityId coordinator = WZ_INVALID_BEHAVIOR_ENTITY;
    };

    struct CombinedInstanceState
    {
        uint32_t init_count = 0;
        uint32_t event_count = 0;
    };

    struct CombinedSharedState
    {
        uint32_t init_count = 0;
        uint32_t event_count = 0;
    };

    struct SharedKeyEdgeProbe
    {
        uint32_t null_create = 0;
        uint32_t empty_create = 0;
        uint32_t null_find_init = 0;
        uint32_t empty_find_init = 0;
        uint32_t null_find_event = 0;
        uint32_t empty_find_event = 0;
    };

    struct ReloadInstanceState
    {
        uint32_t init_count = 0;
        uint32_t event_count = 0;
        uint32_t sentinel = 0;
    };

    struct ReloadSharedState
    {
        uint32_t init_count = 0;
        uint32_t event_count = 0;
        uint32_t sentinel = 0;
    };

    constexpr const char* kSharedGroupKey = "tank_group";
    constexpr const char* kCombinedSharedKey = "combined_state";
    constexpr const char* kResizedSharedKey = "resized_group";
    constexpr const char* kReloadSharedKey = "reload_group";

    InitProbe* g_init_probe = nullptr;
    uint32_t* g_event_only_count = nullptr;
    uint32_t* g_init_only_count = nullptr;
    uint32_t* g_unknown_shared_state_count = nullptr;
    SharedKeyEdgeProbe* g_shared_key_edge_probe = nullptr;
    uint32_t g_resized_shared_state_size = 0;
    uint32_t g_reload_instance_state_size = 0;
    uint32_t g_reload_shared_state_size = 0;

    void on_stateful_init(
        const WzBehaviorInitFacts* facts,
        WzBehaviorEntityId entity,
        void*)
    {
        if (g_init_probe) {
            g_init_probe->init_order.push_back(entity);
        }

        auto* state = static_cast<TestState*>(
            wz_alloc_instance_state(
                facts,
                sizeof(TestState),
                alignof(TestState)));
        if (!state) {
            return;
        }

        ++state->init_count;
        state->entity = entity;
    }

    void on_stateful_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent*,
        void*)
    {
        auto* state = static_cast<TestState*>(
            wz_get_instance_state(facts));
        if (state) {
            ++state->event_count;
        }
    }

    uint8_t register_stateful_pack(WzBehaviorPluginApi* api)
    {
        static const char* events[] = { "frame.update" };
        const WzBehaviorModuleDesc desc{
            .size = sizeof(WzBehaviorModuleDesc),
            .module = "stateful",
            .on_event = on_stateful_event,
            .on_init = on_stateful_init,
            .event_channels = events,
            .event_channel_count = 1u,
            .module_user_data = nullptr,
        };
        return api && api->version == WZ_BEHAVIOR_ABI_VERSION
            && api->register_module_desc
            ? api->register_module_desc(api->user, &desc)
            : 0u;
    }

    void on_event_only_event(
        const WzBehaviorFrameFacts*,
        const WzBehaviorEvent*,
        void*)
    {
        if (g_event_only_count) {
            ++*g_event_only_count;
        }
    }

    uint8_t register_event_only_pack(WzBehaviorPluginApi* api)
    {
        static const char* events[] = { "frame.update" };
        const WzBehaviorModuleDesc desc{
            .size = sizeof(WzBehaviorModuleDesc),
            .module = "event_only",
            .on_event = on_event_only_event,
            .on_init = nullptr,
            .event_channels = events,
            .event_channel_count = 1u,
            .module_user_data = nullptr,
        };
        return api && api->version == WZ_BEHAVIOR_ABI_VERSION
            && api->register_module_desc
            ? api->register_module_desc(api->user, &desc)
            : 0u;
    }

    void on_init_only_init(
        const WzBehaviorInitFacts*,
        WzBehaviorEntityId,
        void*)
    {
        if (g_init_only_count) {
            ++*g_init_only_count;
        }
    }

    uint8_t register_init_only_pack(WzBehaviorPluginApi* api)
    {
        const WzBehaviorModuleDesc desc{
            .size = sizeof(WzBehaviorModuleDesc),
            .module = "init_only",
            .on_event = nullptr,
            .on_init = on_init_only_init,
        };
        return api && api->version == WZ_BEHAVIOR_ABI_VERSION
            && api->register_module_desc
            ? api->register_module_desc(api->user, &desc)
            : 0u;
    }

    void on_shared_coordinator_init(
        const WzBehaviorInitFacts* facts,
        WzBehaviorEntityId entity,
        void*)
    {
        auto* state = static_cast<SharedGroupState*>(
            wz_create_shared_state(
                facts,
                kSharedGroupKey,
                sizeof(SharedGroupState),
                alignof(SharedGroupState)));
        if (!state) {
            return;
        }

        ++state->coordinator_inits;
        state->coordinator = entity;
        if (state->sentinel == 0u) {
            state->sentinel = 0x1234u;
        }
    }

    void on_shared_participant_init(
        const WzBehaviorInitFacts* facts,
        WzBehaviorEntityId,
        void*)
    {
        auto* state = static_cast<SharedGroupState*>(
            wz_find_shared_state(facts, kSharedGroupKey));
        if (state) {
            ++state->participant_inits;
        }
    }

    void on_shared_participant_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent*,
        void*)
    {
        auto* state = static_cast<SharedGroupState*>(
            wz_find_shared_state(facts, kSharedGroupKey));
        if (state) {
            ++state->participant_events;
        }
        if (!wz_find_shared_state(facts, "missing_group")
            && g_unknown_shared_state_count)
        {
            ++*g_unknown_shared_state_count;
        }
    }

    uint8_t register_shared_state_pack(WzBehaviorPluginApi* api)
    {
        static const char* events[] = { "frame.update" };
        const WzBehaviorModuleDesc coordinator{
            .size = sizeof(WzBehaviorModuleDesc),
            .module = "shared_coordinator",
            .on_event = nullptr,
            .on_init = on_shared_coordinator_init,
        };
        const WzBehaviorModuleDesc participant{
            .size = sizeof(WzBehaviorModuleDesc),
            .module = "shared_participant",
            .on_event = on_shared_participant_event,
            .on_init = on_shared_participant_init,
            .event_channels = events,
            .event_channel_count = 1u,
        };
        return api && api->version == WZ_BEHAVIOR_ABI_VERSION
            && api->register_module_desc
            && api->register_module_desc(api->user, &coordinator)
            && api->register_module_desc(api->user, &participant)
            ? uint8_t{ 1 }
            : uint8_t{ 0 };
    }

    void on_combined_state_init(
        const WzBehaviorInitFacts* facts,
        WzBehaviorEntityId,
        void*)
    {
        auto* instance = static_cast<CombinedInstanceState*>(
            wz_alloc_instance_state(
                facts,
                sizeof(CombinedInstanceState),
                alignof(CombinedInstanceState)));
        auto* shared = static_cast<CombinedSharedState*>(
            wz_create_shared_state(
                facts,
                kCombinedSharedKey,
                sizeof(CombinedSharedState),
                alignof(CombinedSharedState)));
        if (instance) {
            ++instance->init_count;
        }
        if (shared) {
            ++shared->init_count;
        }
    }

    void on_combined_state_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent*,
        void*)
    {
        auto* instance = static_cast<CombinedInstanceState*>(
            wz_get_instance_state(facts));
        auto* shared = static_cast<CombinedSharedState*>(
            wz_find_shared_state(facts, kCombinedSharedKey));
        if (instance) {
            ++instance->event_count;
        }
        if (shared) {
            ++shared->event_count;
        }
    }

    uint8_t register_combined_state_pack(WzBehaviorPluginApi* api)
    {
        static const char* events[] = { "frame.update" };
        const WzBehaviorModuleDesc desc{
            .size = sizeof(WzBehaviorModuleDesc),
            .module = "combined_state",
            .on_event = on_combined_state_event,
            .on_init = on_combined_state_init,
            .event_channels = events,
            .event_channel_count = 1u,
        };
        return api && api->version == WZ_BEHAVIOR_ABI_VERSION
            && api->register_module_desc
            ? api->register_module_desc(api->user, &desc)
            : 0u;
    }

    void on_resized_shared_state_init(
        const WzBehaviorInitFacts* facts,
        WzBehaviorEntityId,
        void*)
    {
        const uint32_t size =
            g_resized_shared_state_size > 0u
            ? g_resized_shared_state_size
            : 16u;
        wz_create_shared_state(
            facts,
            kResizedSharedKey,
            size,
            alignof(uint32_t));
    }

    uint8_t register_resized_shared_state_pack(WzBehaviorPluginApi* api)
    {
        const WzBehaviorModuleDesc desc{
            .size = sizeof(WzBehaviorModuleDesc),
            .module = "resized_shared_state",
            .on_event = nullptr,
            .on_init = on_resized_shared_state_init,
        };
        return api && api->version == WZ_BEHAVIOR_ABI_VERSION
            && api->register_module_desc
            ? api->register_module_desc(api->user, &desc)
            : 0u;
    }

    void on_shared_key_edges_init(
        const WzBehaviorInitFacts* facts,
        WzBehaviorEntityId,
        void*)
    {
        if (!g_shared_key_edge_probe) {
            return;
        }
        if (!wz_create_shared_state(
                facts,
                nullptr,
                sizeof(uint32_t),
                alignof(uint32_t)))
        {
            ++g_shared_key_edge_probe->null_create;
        }
        if (!wz_create_shared_state(
                facts,
                "",
                sizeof(uint32_t),
                alignof(uint32_t)))
        {
            ++g_shared_key_edge_probe->empty_create;
        }
        if (!wz_find_shared_state(facts, nullptr)) {
            ++g_shared_key_edge_probe->null_find_init;
        }
        if (!wz_find_shared_state(facts, "")) {
            ++g_shared_key_edge_probe->empty_find_init;
        }
    }

    void on_shared_key_edges_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent*,
        void*)
    {
        if (!g_shared_key_edge_probe) {
            return;
        }
        if (!wz_find_shared_state(facts, nullptr)) {
            ++g_shared_key_edge_probe->null_find_event;
        }
        if (!wz_find_shared_state(facts, "")) {
            ++g_shared_key_edge_probe->empty_find_event;
        }
    }

    uint8_t register_shared_key_edge_pack(WzBehaviorPluginApi* api)
    {
        static const char* events[] = { "frame.update" };
        const WzBehaviorModuleDesc desc{
            .size = sizeof(WzBehaviorModuleDesc),
            .module = "shared_key_edges",
            .on_event = on_shared_key_edges_event,
            .on_init = on_shared_key_edges_init,
            .event_channels = events,
            .event_channel_count = 1u,
        };
        return api && api->version == WZ_BEHAVIOR_ABI_VERSION
            && api->register_module_desc
            ? api->register_module_desc(api->user, &desc)
            : 0u;
    }

    void on_reloadable_init(
        const WzBehaviorInitFacts* facts,
        WzBehaviorEntityId,
        void*)
    {
        const uint32_t instance_size =
            g_reload_instance_state_size > 0u
            ? g_reload_instance_state_size
            : sizeof(ReloadInstanceState);
        const uint32_t shared_size =
            g_reload_shared_state_size > 0u
            ? g_reload_shared_state_size
            : sizeof(ReloadSharedState);

        auto* instance = static_cast<ReloadInstanceState*>(
            wz_alloc_instance_state(
                facts,
                instance_size,
                alignof(ReloadInstanceState)));
        auto* shared = static_cast<ReloadSharedState*>(
            wz_create_shared_state(
                facts,
                kReloadSharedKey,
                shared_size,
                alignof(ReloadSharedState)));
        if (instance && instance_size >= sizeof(ReloadInstanceState)) {
            ++instance->init_count;
        }
        if (shared && shared_size >= sizeof(ReloadSharedState)) {
            ++shared->init_count;
        }
    }

    void on_reloadable_event_v1(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        auto* instance = static_cast<ReloadInstanceState*>(
            wz_get_instance_state(facts));
        auto* shared = static_cast<ReloadSharedState*>(
            wz_find_shared_state(facts, kReloadSharedKey));
        if (instance) {
            ++instance->event_count;
        }
        if (shared) {
            ++shared->event_count;
        }
        wz_self_add_local_translation(facts, event, 0.0f, 1.0f, 0.0f);
    }

    void on_reloadable_event_v2(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        auto* instance = static_cast<ReloadInstanceState*>(
            wz_get_instance_state(facts));
        auto* shared = static_cast<ReloadSharedState*>(
            wz_find_shared_state(facts, kReloadSharedKey));
        if (instance) {
            ++instance->event_count;
        }
        if (shared) {
            ++shared->event_count;
        }
        wz_self_add_local_translation(facts, event, 0.0f, 2.0f, 0.0f);
    }

    uint8_t register_reloadable_pack(
        WzBehaviorPluginApi* api,
        WzBehaviorModuleEventFn on_event)
    {
        static const char* events[] = { "frame.update" };
        const WzBehaviorModuleDesc desc{
            .size = sizeof(WzBehaviorModuleDesc),
            .module = "reloadable",
            .on_event = on_event,
            .on_init = on_reloadable_init,
            .event_channels = events,
            .event_channel_count = 1u,
        };
        return api && api->version == WZ_BEHAVIOR_ABI_VERSION
            && api->register_module_desc
            ? api->register_module_desc(api->user, &desc)
            : 0u;
    }

    uint8_t register_reloadable_pack_without_init(WzBehaviorPluginApi* api)
    {
        static const char* events[] = { "frame.update" };
        const WzBehaviorModuleDesc desc{
            .size = sizeof(WzBehaviorModuleDesc),
            .module = "reloadable",
            .on_event = on_reloadable_event_v2,
            .on_init = nullptr,
            .event_channels = events,
            .event_channel_count = 1u,
        };
        return api && api->version == WZ_BEHAVIOR_ABI_VERSION
            && api->register_module_desc
            ? api->register_module_desc(api->user, &desc)
            : 0u;
    }

    uint8_t register_reloadable_pack_v1(WzBehaviorPluginApi* api)
    {
        return register_reloadable_pack(api, on_reloadable_event_v1);
    }

    uint8_t register_reloadable_pack_v2(WzBehaviorPluginApi* api)
    {
        return register_reloadable_pack(api, on_reloadable_event_v2);
    }

    void on_legacy_desc_event(
        const WzBehaviorFrameFacts*,
        const WzBehaviorEvent*,
        void*)
    {
    }

    uint8_t register_smaller_descriptor_pack(WzBehaviorPluginApi* api)
    {
        const WzBehaviorModuleDesc desc{
            .size = static_cast<uint32_t>(
                offsetof(WzBehaviorModuleDesc, on_init)),
            .module = "smaller_desc",
            .on_event = on_legacy_desc_event,
        };
        return api && api->version == WZ_BEHAVIOR_ABI_VERSION
            && api->register_module_desc
            ? api->register_module_desc(api->user, &desc)
            : 0u;
    }
}

TEST(BehaviorInit, RunsInitInTopologicalOrderAndStateFeedsDispatch)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_init_topological";

    wz::engine::assets::SceneNodeAsset root{};
    root.id = "root";
    root.behavior = wz::engine::assets::SceneBehaviorAsset{
        .id = "root_state",
        .module = "stateful",
    };

    wz::engine::assets::SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "root";
    child.behavior = wz::engine::assets::SceneBehaviorAsset{
        .id = "child_state",
        .module = "stateful",
    };

    asset.nodes.push_back(std::move(root));
    asset.nodes.push_back(std::move(child));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    auto& scene = result.instance;

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    InitProbe probe{};
    g_init_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_stateful_pack));

    initialize_behaviors(scene, registry);

    ASSERT_EQ(probe.init_order.size(), 2u);
    EXPECT_EQ(probe.init_order[0], scene.authored_to_runtime["root"]);
    EXPECT_EQ(probe.init_order[1], scene.authored_to_runtime["child"]);

    auto* root_block =
        scene.behavior_state.find_instance_state("root_state");
    auto* child_block =
        scene.behavior_state.find_instance_state("child_state");
    ASSERT_NE(root_block, nullptr);
    ASSERT_NE(child_block, nullptr);
    auto* root_state = static_cast<TestState*>(root_block->data);
    auto* child_state = static_cast<TestState*>(child_block->data);
    ASSERT_NE(root_state, nullptr);
    ASSERT_NE(child_state, nullptr);
    EXPECT_EQ(root_state->init_count, 1u);
    EXPECT_EQ(child_state->init_count, 1u);

    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .commands = &frame_storage.behavior_commands,
    };
    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(root_state->event_count, 1u);
    EXPECT_EQ(child_state->event_count, 1u);

    g_init_probe = nullptr;
}

TEST(BehaviorInit, MultipleBindingsOnOneNodeGetDistinctInstanceState)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_init_multi_binding";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.behaviors.push_back(wz::engine::assets::SceneBehaviorAsset{
        .id = "drive_state",
        .module = "stateful",
    });
    actor.behaviors.push_back(wz::engine::assets::SceneBehaviorAsset{
        .id = "turret_state",
        .module = "stateful",
    });
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    auto& scene = result.instance;

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    InitProbe probe{};
    g_init_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_stateful_pack));

    initialize_behaviors(scene, registry);

    auto* drive =
        scene.behavior_state.find_instance_state("drive_state");
    auto* turret =
        scene.behavior_state.find_instance_state("turret_state");
    ASSERT_NE(drive, nullptr);
    ASSERT_NE(turret, nullptr);
    EXPECT_NE(drive->data, turret->data);
    EXPECT_EQ(
        static_cast<TestState*>(drive->data)->entity,
        scene.authored_to_runtime["actor"]);
    EXPECT_EQ(
        static_cast<TestState*>(turret->data)->entity,
        scene.authored_to_runtime["actor"]);
    EXPECT_EQ(probe.init_order.size(), 2u);

    g_init_probe = nullptr;
}

TEST(BehaviorInit, CompatibleInstanceStateSurvivesRepeatedInit)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_init_repeated";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .id = "actor_state",
        .module = "stateful",
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    auto& scene = result.instance;

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_stateful_pack));

    initialize_behaviors(scene, registry);

    auto* first_block =
        scene.behavior_state.find_instance_state("actor_state");
    ASSERT_NE(first_block, nullptr);
    auto* state = static_cast<TestState*>(first_block->data);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->init_count, 1u);

    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .commands = &frame_storage.behavior_commands,
    };
    dispatch_behaviors(scene, registry, context);
    EXPECT_EQ(state->event_count, 1u);
    state->sentinel = 0xc0ffeeu;

    initialize_behaviors(scene, registry);

    auto* second_block =
        scene.behavior_state.find_instance_state("actor_state");
    ASSERT_NE(second_block, nullptr);
    EXPECT_EQ(second_block->data, first_block->data);
    EXPECT_EQ(state->init_count, 2u);
    EXPECT_EQ(state->event_count, 1u);
    EXPECT_EQ(state->sentinel, 0xc0ffeeu);
}

TEST(BehaviorInit, ModuleWithoutInitStillDispatchesEvents)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_init_event_only";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .id = "actor_event_only",
        .module = "event_only",
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    auto& scene = result.instance;

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    uint32_t event_count = 0;
    g_event_only_count = &event_count;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_event_only_pack));

    initialize_behaviors(scene, registry);

    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .commands = &frame_storage.behavior_commands,
    };
    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(event_count, 1u);
    EXPECT_EQ(scene.behavior_state.find_instance_state("actor_event_only"),
        nullptr);

    g_event_only_count = nullptr;
}

TEST(BehaviorInit, InitOnlyModuleRunsInitAndDispatchSkipsCleanly)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_init_init_only";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .id = "actor_init_only",
        .module = "init_only",
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    auto& scene = result.instance;

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    uint32_t init_count = 0;
    g_init_only_count = &init_count;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_init_only_pack));

    initialize_behaviors(scene, registry);

    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .commands = &frame_storage.behavior_commands,
    };
    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(init_count, 1u);
    EXPECT_TRUE(frame_storage.behavior_commands.commands.empty());

    g_init_only_count = nullptr;
}

TEST(BehaviorInit, SharedStateCreatedInInitAndFoundDuringDispatch)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_init_shared_state";

    wz::engine::assets::SceneNodeAsset root{};
    root.id = "root";
    root.behavior = wz::engine::assets::SceneBehaviorAsset{
        .id = "coordinator",
        .module = "shared_coordinator",
    };

    wz::engine::assets::SceneNodeAsset tank_a{};
    tank_a.id = "tank_a";
    tank_a.parent_id = "root";
    tank_a.behavior = wz::engine::assets::SceneBehaviorAsset{
        .id = "tank_a_participant",
        .module = "shared_participant",
    };

    wz::engine::assets::SceneNodeAsset tank_b{};
    tank_b.id = "tank_b";
    tank_b.parent_id = "root";
    tank_b.behavior = wz::engine::assets::SceneBehaviorAsset{
        .id = "tank_b_participant",
        .module = "shared_participant",
    };

    asset.nodes.push_back(std::move(root));
    asset.nodes.push_back(std::move(tank_a));
    asset.nodes.push_back(std::move(tank_b));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    auto& scene = result.instance;

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    uint32_t unknown_shared_state_count = 0;
    g_unknown_shared_state_count = &unknown_shared_state_count;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_shared_state_pack));

    initialize_behaviors(scene, registry);

    auto* block =
        scene.behavior_state.find_shared_state(kSharedGroupKey);
    ASSERT_NE(block, nullptr);
    auto* state = static_cast<SharedGroupState*>(block->data);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->coordinator_inits, 1u);
    EXPECT_EQ(state->participant_inits, 2u);
    EXPECT_EQ(state->participant_events, 0u);
    EXPECT_EQ(state->coordinator, scene.authored_to_runtime["root"]);
    EXPECT_EQ(state->sentinel, 0x1234u);

    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .commands = &frame_storage.behavior_commands,
    };
    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(state->participant_events, 2u);
    EXPECT_EQ(unknown_shared_state_count, 2u);

    g_unknown_shared_state_count = nullptr;
}

TEST(BehaviorInit, CompatibleSharedStateSurvivesRepeatedInit)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_init_shared_state_repeated";

    wz::engine::assets::SceneNodeAsset root{};
    root.id = "root";
    root.behavior = wz::engine::assets::SceneBehaviorAsset{
        .id = "coordinator",
        .module = "shared_coordinator",
    };
    asset.nodes.push_back(std::move(root));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    auto& scene = result.instance;

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_shared_state_pack));

    initialize_behaviors(scene, registry);

    auto* first_block =
        scene.behavior_state.find_shared_state(kSharedGroupKey);
    ASSERT_NE(first_block, nullptr);
    auto* state = static_cast<SharedGroupState*>(first_block->data);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->coordinator_inits, 1u);
    state->sentinel = 0xc0ffeeu;

    initialize_behaviors(scene, registry);

    auto* second_block =
        scene.behavior_state.find_shared_state(kSharedGroupKey);
    ASSERT_NE(second_block, nullptr);
    EXPECT_EQ(second_block->data, first_block->data);
    EXPECT_EQ(state->coordinator_inits, 2u);
    EXPECT_EQ(state->sentinel, 0xc0ffeeu);
}

TEST(BehaviorInit, IncompatibleSharedStateResetLogsWarning)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_init_shared_state_resize";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .id = "resizer",
        .module = "resized_shared_state",
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    auto& scene = result.instance;

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_resized_shared_state_pack));

    wz::logging::internal::MemoryLogSink sink;
    wz::Logger logger;
    ASSERT_TRUE(wz::logging::init_logger(
        logger,
        { wz::LogLevel::Debug, false, &sink }));

    g_resized_shared_state_size = 16u;
    initialize_behaviors(scene, registry, &logger);
    EXPECT_EQ(sink.size(), 0u);

    g_resized_shared_state_size = 32u;
    initialize_behaviors(scene, registry, &logger);
    wz::logging::wait_until_idle(logger);

    const auto messages = sink.snapshot();
    ASSERT_EQ(messages.size(), 1u);
    EXPECT_EQ(messages[0].level, wz::LogLevel::Warning);
    EXPECT_NE(
        std::string(messages[0].text).find(
            "behavior shared state reset for key 'resized_group'"),
        std::string::npos);

    g_resized_shared_state_size = 0u;
    wz::logging::shutdown_logger(logger);
}

TEST(BehaviorInit, SharedStateNullAndEmptyKeysReturnNull)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_init_shared_key_edges";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .id = "key_edges",
        .module = "shared_key_edges",
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    auto& scene = result.instance;

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    SharedKeyEdgeProbe probe{};
    g_shared_key_edge_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_shared_key_edge_pack));

    initialize_behaviors(scene, registry);

    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .commands = &frame_storage.behavior_commands,
    };
    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(probe.null_create, 1u);
    EXPECT_EQ(probe.empty_create, 1u);
    EXPECT_EQ(probe.null_find_init, 1u);
    EXPECT_EQ(probe.empty_find_init, 1u);
    EXPECT_EQ(probe.null_find_event, 1u);
    EXPECT_EQ(probe.empty_find_event, 1u);
    EXPECT_TRUE(scene.behavior_state.shared_state.empty());

    g_shared_key_edge_probe = nullptr;
}

TEST(BehaviorInit, OneBindingCanUseInstanceAndSharedState)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_init_combined_state";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .id = "combined",
        .module = "combined_state",
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    auto& scene = result.instance;

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_combined_state_pack));

    initialize_behaviors(scene, registry);

    auto* instance_block =
        scene.behavior_state.find_instance_state("combined");
    auto* shared_block =
        scene.behavior_state.find_shared_state(kCombinedSharedKey);
    ASSERT_NE(instance_block, nullptr);
    ASSERT_NE(shared_block, nullptr);
    ASSERT_NE(instance_block->data, shared_block->data);

    auto* instance =
        static_cast<CombinedInstanceState*>(instance_block->data);
    auto* shared =
        static_cast<CombinedSharedState*>(shared_block->data);
    ASSERT_NE(instance, nullptr);
    ASSERT_NE(shared, nullptr);
    EXPECT_EQ(instance->init_count, 1u);
    EXPECT_EQ(shared->init_count, 1u);

    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .commands = &frame_storage.behavior_commands,
    };
    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(instance->event_count, 1u);
    EXPECT_EQ(shared->event_count, 1u);
}

TEST(BehaviorInit, ParticipantBeforeCoordinatorDoesNotFindSharedStateInInit)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_init_shared_state_reverse_order";

    wz::engine::assets::SceneNodeAsset participant{};
    participant.id = "participant";
    participant.behavior = wz::engine::assets::SceneBehaviorAsset{
        .id = "participant_binding",
        .module = "shared_participant",
    };

    wz::engine::assets::SceneNodeAsset coordinator{};
    coordinator.id = "coordinator";
    coordinator.parent_id = "participant";
    coordinator.behavior = wz::engine::assets::SceneBehaviorAsset{
        .id = "coordinator_binding",
        .module = "shared_coordinator",
    };

    asset.nodes.push_back(std::move(participant));
    asset.nodes.push_back(std::move(coordinator));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    auto& scene = result.instance;

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_shared_state_pack));

    initialize_behaviors(scene, registry);

    auto* block =
        scene.behavior_state.find_shared_state(kSharedGroupKey);
    ASSERT_NE(block, nullptr);
    auto* state = static_cast<SharedGroupState*>(block->data);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->coordinator_inits, 1u);
    EXPECT_EQ(state->participant_inits, 0u);
    EXPECT_EQ(state->coordinator, scene.authored_to_runtime["coordinator"]);

    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .commands = &frame_storage.behavior_commands,
    };
    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(state->participant_events, 1u);
}

TEST(BehaviorInit, ReRegistrationRerunsInitPreservesStateAndUpdatesEvent)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_init_reload_compatible";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .id = "reload_binding",
        .module = "reloadable",
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    auto& scene = result.instance;

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    g_reload_instance_state_size = sizeof(ReloadInstanceState);
    g_reload_shared_state_size = sizeof(ReloadSharedState);
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_reloadable_pack_v1));
    const auto first_handle = registry.find_module("reloadable");
    ASSERT_TRUE(first_handle.has_value());

    initialize_behaviors(scene, registry);

    auto* instance_block =
        scene.behavior_state.find_instance_state("reload_binding");
    auto* shared_block =
        scene.behavior_state.find_shared_state(kReloadSharedKey);
    ASSERT_NE(instance_block, nullptr);
    ASSERT_NE(shared_block, nullptr);
    auto* instance =
        static_cast<ReloadInstanceState*>(instance_block->data);
    auto* shared =
        static_cast<ReloadSharedState*>(shared_block->data);
    ASSERT_NE(instance, nullptr);
    ASSERT_NE(shared, nullptr);
    EXPECT_EQ(instance->init_count, 1u);
    EXPECT_EQ(shared->init_count, 1u);
    instance->sentinel = 0xc0ffeeu;
    shared->sentinel = 0xbeefu;

    wz::engine::FrameStorage first_frame{};
    BehaviorFrameContext first_context{
        .frame_storage = &first_frame,
        .commands = &first_frame.behavior_commands,
    };
    dispatch_behaviors(scene, registry, first_context);
    ASSERT_EQ(first_frame.behavior_commands.commands.size(), 1u);
    EXPECT_FLOAT_EQ(first_frame.behavior_commands.commands[0].values[1], 1.0f);

    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_reloadable_pack_v2));
    EXPECT_EQ(registry.modules().size(), 1u);
    const auto second_handle = registry.find_module("reloadable");
    ASSERT_TRUE(second_handle.has_value());
    EXPECT_EQ(second_handle->index, first_handle->index);

    initialize_behaviors(scene, registry);

    auto* second_instance_block =
        scene.behavior_state.find_instance_state("reload_binding");
    auto* second_shared_block =
        scene.behavior_state.find_shared_state(kReloadSharedKey);
    ASSERT_NE(second_instance_block, nullptr);
    ASSERT_NE(second_shared_block, nullptr);
    EXPECT_EQ(second_instance_block->data, instance_block->data);
    EXPECT_EQ(second_shared_block->data, shared_block->data);
    EXPECT_EQ(instance->init_count, 2u);
    EXPECT_EQ(shared->init_count, 2u);
    EXPECT_EQ(instance->sentinel, 0xc0ffeeu);
    EXPECT_EQ(shared->sentinel, 0xbeefu);

    wz::engine::FrameStorage second_frame{};
    BehaviorFrameContext second_context{
        .frame_storage = &second_frame,
        .commands = &second_frame.behavior_commands,
    };
    dispatch_behaviors(scene, registry, second_context);
    ASSERT_EQ(second_frame.behavior_commands.commands.size(), 1u);
    EXPECT_FLOAT_EQ(
        second_frame.behavior_commands.commands[0].values[1],
        2.0f);
    EXPECT_EQ(instance->event_count, 2u);
    EXPECT_EQ(shared->event_count, 2u);

    g_reload_instance_state_size = 0u;
    g_reload_shared_state_size = 0u;
}

TEST(BehaviorInit, StateAllocatedByOldInitSurvivesWhenReloadRemovesInit)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_init_reload_removes_init";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .id = "reload_binding",
        .module = "reloadable",
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    auto& scene = result.instance;

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    g_reload_instance_state_size = sizeof(ReloadInstanceState);
    g_reload_shared_state_size = sizeof(ReloadSharedState);
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_reloadable_pack_v1));

    initialize_behaviors(scene, registry);

    auto* instance_block =
        scene.behavior_state.find_instance_state("reload_binding");
    auto* shared_block =
        scene.behavior_state.find_shared_state(kReloadSharedKey);
    ASSERT_NE(instance_block, nullptr);
    ASSERT_NE(shared_block, nullptr);
    auto* instance =
        static_cast<ReloadInstanceState*>(instance_block->data);
    auto* shared =
        static_cast<ReloadSharedState*>(shared_block->data);
    ASSERT_NE(instance, nullptr);
    ASSERT_NE(shared, nullptr);
    EXPECT_EQ(instance->init_count, 1u);
    EXPECT_EQ(shared->init_count, 1u);
    instance->sentinel = 0xc0ffeeu;
    shared->sentinel = 0xbeefu;

    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_reloadable_pack_without_init));
    const auto handle = registry.find_module("reloadable");
    ASSERT_TRUE(handle.has_value());
    const auto* module = registry.get_module(*handle);
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->on_init, nullptr);

    initialize_behaviors(scene, registry);

    auto* second_instance_block =
        scene.behavior_state.find_instance_state("reload_binding");
    auto* second_shared_block =
        scene.behavior_state.find_shared_state(kReloadSharedKey);
    ASSERT_NE(second_instance_block, nullptr);
    ASSERT_NE(second_shared_block, nullptr);
    EXPECT_EQ(second_instance_block->data, instance_block->data);
    EXPECT_EQ(second_shared_block->data, shared_block->data);
    EXPECT_EQ(instance->init_count, 1u);
    EXPECT_EQ(shared->init_count, 1u);
    EXPECT_EQ(instance->sentinel, 0xc0ffeeu);
    EXPECT_EQ(shared->sentinel, 0xbeefu);

    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .commands = &frame_storage.behavior_commands,
    };
    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u);
    EXPECT_FLOAT_EQ(
        frame_storage.behavior_commands.commands[0].values[1],
        2.0f);
    EXPECT_EQ(instance->event_count, 1u);
    EXPECT_EQ(shared->event_count, 1u);

    g_reload_instance_state_size = 0u;
    g_reload_shared_state_size = 0u;
}

TEST(BehaviorInit, ReRegistrationResetsIncompatibleStateAndLogs)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_init_reload_incompatible";

    wz::engine::assets::SceneNodeAsset actor{};
    actor.id = "actor";
    actor.behavior = wz::engine::assets::SceneBehaviorAsset{
        .id = "reload_binding",
        .module = "reloadable",
    };
    asset.nodes.push_back(std::move(actor));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    auto& scene = result.instance;

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    wz::logging::internal::MemoryLogSink sink;
    wz::Logger logger;
    ASSERT_TRUE(wz::logging::init_logger(
        logger,
        { wz::LogLevel::Debug, false, &sink }));

    g_reload_instance_state_size = sizeof(ReloadInstanceState);
    g_reload_shared_state_size = sizeof(ReloadSharedState);
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_reloadable_pack_v1,
        &logger));

    initialize_behaviors(scene, registry, &logger);

    auto* instance_block =
        scene.behavior_state.find_instance_state("reload_binding");
    auto* shared_block =
        scene.behavior_state.find_shared_state(kReloadSharedKey);
    ASSERT_NE(instance_block, nullptr);
    ASSERT_NE(shared_block, nullptr);
    auto* instance =
        static_cast<ReloadInstanceState*>(instance_block->data);
    auto* shared =
        static_cast<ReloadSharedState*>(shared_block->data);
    ASSERT_NE(instance, nullptr);
    ASSERT_NE(shared, nullptr);
    instance->sentinel = 0xc0ffeeu;
    shared->sentinel = 0xbeefu;

    g_reload_instance_state_size =
        sizeof(ReloadInstanceState) + sizeof(uint32_t);
    g_reload_shared_state_size =
        sizeof(ReloadSharedState) + sizeof(uint32_t);
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_reloadable_pack_v2,
        &logger));

    initialize_behaviors(scene, registry, &logger);
    wz::logging::wait_until_idle(logger);

    const auto messages = sink.snapshot();
    ASSERT_EQ(messages.size(), 2u);
    EXPECT_EQ(messages[0].level, wz::LogLevel::Warning);
    EXPECT_EQ(messages[1].level, wz::LogLevel::Warning);
    EXPECT_NE(
        std::string(messages[0].text).find(
            "behavior instance state reset for binding 'reload_binding'"),
        std::string::npos);
    EXPECT_NE(
        std::string(messages[1].text).find(
            "behavior shared state reset for key 'reload_group'"),
        std::string::npos);

    auto* reset_instance_block =
        scene.behavior_state.find_instance_state("reload_binding");
    auto* reset_shared_block =
        scene.behavior_state.find_shared_state(kReloadSharedKey);
    ASSERT_NE(reset_instance_block, nullptr);
    ASSERT_NE(reset_shared_block, nullptr);
    auto* reset_instance =
        static_cast<ReloadInstanceState*>(reset_instance_block->data);
    auto* reset_shared =
        static_cast<ReloadSharedState*>(reset_shared_block->data);
    ASSERT_NE(reset_instance, nullptr);
    ASSERT_NE(reset_shared, nullptr);
    EXPECT_EQ(reset_instance_block->size, g_reload_instance_state_size);
    EXPECT_EQ(reset_shared_block->size, g_reload_shared_state_size);
    EXPECT_EQ(reset_instance->init_count, 1u);
    EXPECT_EQ(reset_shared->init_count, 1u);
    EXPECT_EQ(reset_instance->sentinel, 0u);
    EXPECT_EQ(reset_shared->sentinel, 0u);

    g_reload_instance_state_size = 0u;
    g_reload_shared_state_size = 0u;
    wz::logging::shutdown_logger(logger);
}

TEST(BehaviorInit, DescriptorRangeValidationAllowsMissingInitField)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;

    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_smaller_descriptor_pack));

    const auto handle = registry.find_module("smaller_desc");
    ASSERT_TRUE(handle.has_value());
    const auto* module = registry.get_module(*handle);
    ASSERT_NE(module, nullptr);
    EXPECT_NE(module->on_event, nullptr);
    EXPECT_EQ(module->on_init, nullptr);
}
