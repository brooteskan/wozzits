#include "behavior_test_support.h"

#include <engine/behavior/behavior_plugin_adapter.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{
    struct TestState
    {
        uint32_t init_count = 0;
        uint32_t event_count = 0;
        WzBehaviorEntityId entity = WZ_INVALID_BEHAVIOR_ENTITY;
    };

    struct InitProbe
    {
        std::vector<WzBehaviorEntityId> init_order;
    };

    InitProbe* g_init_probe = nullptr;

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

    initialize_behaviors(scene, registry);

    auto* second_block =
        scene.behavior_state.find_instance_state("actor_state");
    ASSERT_NE(second_block, nullptr);
    EXPECT_EQ(second_block->data, first_block->data);
    EXPECT_EQ(state->init_count, 2u);
    EXPECT_EQ(state->event_count, 1u);
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
