#include <engine/behavior/behavior_dispatch.h>
#include <engine/behavior/behavior_registry.h>
#include <engine/assets/engine_asset_library.h>
#include <engine/assets/scene/scene_json_export.h>
#include <engine/assets/scene/scene_instance.h>
#include <engine/frame_storage.h>

#include <external/json/json_writer.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

#include <gtest/gtest.h>

#include <string>
#include <utility>

namespace
{
    using namespace wz::engine::behavior;
    using wz::engine::assets::BehaviorComponent;
    using wz::engine::assets::SceneComponentRecord;
    using wz::engine::assets::SceneInstance;
    using wz::scene::RuntimeEntityId;

    struct CallCounter
    {
        uint32_t calls = 0;
        RuntimeEntityId last_entity = wz::scene::INVALID_RUNTIME_ENTITY;
    };

    void count_behavior(
        BehaviorFrameContext& context,
        RuntimeEntityId entity,
        void* user_data)
    {
        auto* counter = static_cast<CallCounter*>(user_data);
        ASSERT_NE(counter, nullptr);
        ASSERT_NE(context.commands, nullptr);

        ++counter->calls;
        counter->last_entity = entity;
        context.commands->add_world_translation(entity, 1.0f, 2.0f, 3.0f);
    }

    void bounce_on_collision_enter(
        BehaviorFrameContext& context,
        RuntimeEntityId entity,
        void*)
    {
        ASSERT_NE(context.frame_storage, nullptr);
        ASSERT_NE(context.commands, nullptr);

        for (const auto& event :
            context.frame_storage->collision.routed_entity_events)
        {
            if (event.entity == entity
                && event.kind
                    == wz::engine::collision::CollisionEventKind::Enter)
            {
                context.commands->add_world_translation(
                    entity,
                    0.0f,
                    8.0f,
                    0.0f);
            }
        }
    }

    SceneInstance scene_with_behavior(
        RuntimeEntityId entity,
        std::string module,
        std::string name,
        bool enabled = true)
    {
        SceneInstance scene{};
        scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
            .node = entity,
            .component = BehaviorComponent{
                .module = std::move(module),
                .name = std::move(name),
                .enabled = enabled,
            },
        });
        return scene;
    }

    wz::fs::Path write_text(
        const wz::fs::Path& root,
        const std::string& filename,
        const std::string& content)
    {
        const wz::fs::Path path = wz::fs::join(root, filename);
        wz::fs::write_file_text(path, content);
        return filename;
    }
}

TEST(BehaviorRegistry, RegistersAndFindsStaticBehavior)
{
    BehaviorRegistry registry;
    CallCounter counter{};

    const BehaviorHandle handle = registry.register_behavior(
        "gameplay",
        "count",
        count_behavior,
        &counter);

    ASSERT_TRUE(handle.valid());
    const auto found = registry.find("gameplay", "count");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->index, handle.index);

    const BehaviorRegistration* registration = registry.get(handle);
    ASSERT_NE(registration, nullptr);
    EXPECT_EQ(registration->module, "gameplay");
    EXPECT_EQ(registration->name, "count");
    EXPECT_EQ(registration->function, count_behavior);
    EXPECT_EQ(registration->user_data, &counter);
}

TEST(BehaviorRegistry, ReRegisteringBehaviorUpdatesFunctionSlot)
{
    BehaviorRegistry registry;
    CallCounter first{};
    CallCounter second{};

    const BehaviorHandle a = registry.register_behavior(
        "gameplay",
        "count",
        count_behavior,
        &first);
    const BehaviorHandle b = registry.register_behavior(
        "gameplay",
        "count",
        count_behavior,
        &second);

    EXPECT_EQ(a.index, b.index);
    ASSERT_EQ(registry.registrations().size(), 1u);
    EXPECT_EQ(registry.get(a)->user_data, &second);
}

TEST(BehaviorDispatch, RunsEnabledSceneBehaviorAndWritesCommands)
{
    BehaviorRegistry registry;
    CallCounter counter{};
    registry.register_behavior("count", count_behavior, &counter);

    SceneInstance scene = scene_with_behavior(7u, "", "count");
    wz::engine::FrameContext frame_context{};
    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(counter.calls, 1u);
    EXPECT_EQ(counter.last_entity, 7u);
    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u);
    const auto& command = frame_storage.behavior_commands.commands[0];
    EXPECT_EQ(command.entity, 7u);
    EXPECT_EQ(command.kind, BehaviorCommandKind::AddWorldTranslation);
    EXPECT_FLOAT_EQ(command.values[0], 1.0f);
    EXPECT_FLOAT_EQ(command.values[1], 2.0f);
    EXPECT_FLOAT_EQ(command.values[2], 3.0f);
}

TEST(BehaviorDispatch, SkipsDisabledOrMissingBehaviors)
{
    BehaviorRegistry registry;
    CallCounter counter{};
    registry.register_behavior("count", count_behavior, &counter);

    SceneInstance scene{};
    scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
        .node = 1u,
        .component = BehaviorComponent{
            .name = "count",
            .enabled = false,
        },
    });
    scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
        .node = 2u,
        .component = BehaviorComponent{
            .name = "missing",
            .enabled = true,
        },
    });

    wz::engine::FrameContext frame_context{};
    wz::engine::FrameStorage frame_storage{};
    frame_storage.behavior_commands.add_world_translation(
        99u,
        1.0f,
        1.0f,
        1.0f);
    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(counter.calls, 0u);
    EXPECT_TRUE(frame_storage.behavior_commands.commands.empty())
        << "dispatch clears stale behavior commands each frame";
}

TEST(BehaviorDispatch, BehaviorConsumesRoutedCollisionFacts)
{
    BehaviorRegistry registry;
    registry.register_behavior(
        "gameplay",
        "bounce_on_collision",
        bounce_on_collision_enter);

    SceneInstance scene =
        scene_with_behavior(4u, "gameplay", "bounce_on_collision");
    wz::engine::FrameContext frame_context{};
    wz::engine::FrameStorage frame_storage{};
    frame_storage.collision.routed_entity_events = {
        wz::engine::collision::CollisionEntityEvent{
            .entity = 4u,
            .other = 9u,
            .kind = wz::engine::collision::CollisionEventKind::Enter,
        },
        wz::engine::collision::CollisionEntityEvent{
            .entity = 5u,
            .other = 4u,
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

    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u);
    const auto& command = frame_storage.behavior_commands.commands[0];
    EXPECT_EQ(command.entity, 4u);
    EXPECT_EQ(command.kind, BehaviorCommandKind::AddWorldTranslation);
    EXPECT_FLOAT_EQ(command.values[0], 0.0f);
    EXPECT_FLOAT_EQ(command.values[1], 8.0f);
    EXPECT_FLOAT_EQ(command.values[2], 0.0f);
}

TEST(BehaviorDispatch, SceneBehaviorComponentInstantiates)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "behavior_scene";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.behavior = wz::engine::assets::SceneBehaviorAsset{
        .module = "gameplay",
        .name = "bounce_on_collision",
        .enabled = true,
    };
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);

    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_EQ(result.instance.behaviors.size(), 1u);
    EXPECT_EQ(result.instance.behaviors[0].node,
        result.instance.authored_to_runtime["actor"]);
    EXPECT_EQ(result.instance.behaviors[0].component.module, "gameplay");
    EXPECT_EQ(
        result.instance.behaviors[0].component.name,
        "bounce_on_collision");
    EXPECT_TRUE(result.instance.behaviors[0].component.enabled);
}

TEST(BehaviorDispatch, SceneBehaviorJsonRoundTrips)
{
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_behavior_scene_json_roundtrip");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const std::string json = R"({
  "schema": "wozzits.scene.v0",
  "name": "behavior_scene",
  "nodes": [
    {
      "id": "actor",
      "behavior": {
        "module": "gameplay",
        "name": "bounce_on_collision",
        "enabled": true
      }
    }
  ]
})";

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const wz::fs::Path rel_path =
        write_text(root, "behavior.scene.json", json);
    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "behavior_scene",
        .path = rel_path,
    });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto handle = assets.scenes().get_scene(scene_asset);
    ASSERT_TRUE(handle.valid());
    const auto* scene_data = assets.scenes().get_scene_data(handle);
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);
    ASSERT_TRUE(scene_data->nodes[0].behavior.has_value());
    EXPECT_EQ(scene_data->nodes[0].behavior->module, "gameplay");
    EXPECT_EQ(scene_data->nodes[0].behavior->name, "bounce_on_collision");
    EXPECT_TRUE(scene_data->nodes[0].behavior->enabled);

    const auto result = wz::engine::assets::instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_EQ(result.instance.behaviors.size(), 1u);

    const std::string exported = wz::json::serialize_json(
        wz::engine::assets::export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"behavior\""), std::string::npos);
    EXPECT_NE(exported.find("\"module\""), std::string::npos);
    EXPECT_NE(exported.find("\"bounce_on_collision\""), std::string::npos);
}
