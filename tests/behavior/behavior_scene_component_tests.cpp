#include "behavior_test_support.h"

#include <engine/assets/scene/scene_fingerprint.h>

#include <cstdint>

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
    EXPECT_EQ(
        result.instance.behaviors[0].component.binding_id,
        "actor/behavior/0");
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
        "id": "bounce_behavior",
        "module": "gameplay",
        "name": "bounce_on_collision",
        "enabled": true,
        "config": {
          "terrain_id": "terrain",
          "speed": 4.5,
          "snap_to_ground": true
        }
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
    EXPECT_EQ(scene_data->nodes[0].behavior->id, "bounce_behavior");
    EXPECT_EQ(scene_data->nodes[0].behavior->module, "gameplay");
    EXPECT_EQ(scene_data->nodes[0].behavior->name, "bounce_on_collision");
    EXPECT_TRUE(scene_data->nodes[0].behavior->enabled);
    ASSERT_EQ(scene_data->nodes[0].behavior->config.size(), 3u);
    EXPECT_EQ(scene_data->nodes[0].behavior->config[0].key, "terrain_id");
    EXPECT_EQ(
        scene_data->nodes[0].behavior->config[0].kind,
        wz::engine::assets::SceneBehaviorConfigValueKind::String);
    EXPECT_EQ(
        scene_data->nodes[0].behavior->config[0].string_value,
        "terrain");
    EXPECT_EQ(scene_data->nodes[0].behavior->config[1].key, "speed");
    EXPECT_EQ(
        scene_data->nodes[0].behavior->config[1].kind,
        wz::engine::assets::SceneBehaviorConfigValueKind::Number);
    EXPECT_DOUBLE_EQ(
        scene_data->nodes[0].behavior->config[1].number_value,
        4.5);
    EXPECT_EQ(scene_data->nodes[0].behavior->config[2].key, "snap_to_ground");
    EXPECT_EQ(
        scene_data->nodes[0].behavior->config[2].kind,
        wz::engine::assets::SceneBehaviorConfigValueKind::Bool);
    EXPECT_TRUE(scene_data->nodes[0].behavior->config[2].bool_value);

    const auto result = wz::engine::assets::instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_EQ(result.instance.behaviors.size(), 1u);
    EXPECT_EQ(
        result.instance.behaviors[0].component.binding_id,
        "bounce_behavior");
    ASSERT_EQ(result.instance.behaviors[0].component.config.size(), 3u);

    const std::string exported = wz::json::serialize_json(
        wz::engine::assets::export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"behavior\""), std::string::npos);
    EXPECT_NE(exported.find("\"id\""), std::string::npos);
    EXPECT_NE(exported.find("\"bounce_behavior\""), std::string::npos);
    EXPECT_NE(exported.find("\"config\""), std::string::npos);
    EXPECT_NE(exported.find("\"module\""), std::string::npos);
    EXPECT_NE(exported.find("\"bounce_on_collision\""), std::string::npos);
    EXPECT_NE(exported.find("\"terrain_id\""), std::string::npos);
    EXPECT_NE(exported.find("\"speed\""), std::string::npos);
    EXPECT_NE(exported.find("\"snap_to_ground\""), std::string::npos);
}

TEST(BehaviorDispatch, SceneBehaviorJsonAcceptsEventModuleWithoutName)
{
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_behavior_scene_json_event_module");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const std::string json = R"({
  "schema": "wozzits.scene.v0",
  "name": "behavior_scene",
  "nodes": [
    {
      "id": "actor",
      "behavior": {
        "module": "test_behavior",
        "name": "",
        "enabled": true
      }
    }
  ]
})";

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const wz::fs::Path rel_path =
        write_text(root, "behavior_event_module.scene.json", json);
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
    EXPECT_EQ(scene_data->nodes[0].behavior->module, "test_behavior");
    EXPECT_TRUE(scene_data->nodes[0].behavior->name.empty());
    EXPECT_TRUE(scene_data->nodes[0].behavior->enabled);
}

TEST(BehaviorDispatch, SceneBehaviorJsonAcceptsPluralBehaviorBindingsWithEvents)
{
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_behavior_scene_json_plural_behaviors");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const std::string json = R"({
  "schema": "wozzits.scene.v0",
  "name": "behavior_scene",
  "nodes": [
    {
      "id": "actor",
      "behaviors": [
        {
          "label": "Player movement",
          "module": "player_move",
          "events": [ "input.*", "frame.update" ]
        },
        {
          "label": "Footstep audio",
          "module": "footstep_audio",
          "events": [ "collision.enter" ],
          "config": {
            "volume": 0.75
          }
        }
      ]
    }
  ]
})";

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const wz::fs::Path rel_path =
        write_text(root, "behavior_plural.scene.json", json);
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
    EXPECT_FALSE(scene_data->nodes[0].behavior.has_value());
    ASSERT_EQ(scene_data->nodes[0].behaviors.size(), 2u);
    EXPECT_EQ(scene_data->nodes[0].behaviors[0].label, "Player movement");
    EXPECT_EQ(scene_data->nodes[0].behaviors[0].module, "player_move");
    ASSERT_EQ(scene_data->nodes[0].behaviors[0].events.size(), 2u);
    EXPECT_EQ(scene_data->nodes[0].behaviors[0].events[0], "input.*");
    EXPECT_EQ(scene_data->nodes[0].behaviors[1].module, "footstep_audio");
    EXPECT_EQ(scene_data->nodes[0].behaviors[1].label, "Footstep audio");
    ASSERT_EQ(scene_data->nodes[0].behaviors[1].events.size(), 1u);
    EXPECT_EQ(scene_data->nodes[0].behaviors[1].events[0], "collision.enter");

    const auto result = wz::engine::assets::instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_EQ(result.instance.behaviors.size(), 2u);
    EXPECT_EQ(
        result.instance.behaviors[0].component.binding_id,
        "actor/behavior/0");
    EXPECT_EQ(
        result.instance.behaviors[1].component.binding_id,
        "actor/behavior/1");
    EXPECT_EQ(
        result.instance.behaviors[0].component.channel_mask,
        wz::engine::behavior::kInputEventChannels
            | wz::engine::behavior::EventChannelFrameUpdate);
    EXPECT_EQ(
        result.instance.behaviors[1].component.channel_mask,
        wz::engine::behavior::EventChannelCollisionEnter);

    const std::string exported = wz::json::serialize_json(
        wz::engine::assets::export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"behaviors\""), std::string::npos);
    EXPECT_NE(exported.find("\"label\""), std::string::npos);
    EXPECT_NE(exported.find("\"Player movement\""), std::string::npos);
    EXPECT_NE(exported.find("\"events\""), std::string::npos);
    EXPECT_NE(exported.find("\"player_move\""), std::string::npos);
    EXPECT_NE(exported.find("\"footstep_audio\""), std::string::npos);
}

TEST(BehaviorDispatch, DuplicateBehaviorBindingIdsAreRejected)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "duplicate_behavior_binding_ids";

    wz::engine::assets::SceneNodeAsset node{};
    node.id = "actor";
    node.behaviors.push_back(wz::engine::assets::SceneBehaviorAsset{
        .id = "tank_control",
        .module = "tank",
    });
    node.behaviors.push_back(wz::engine::assets::SceneBehaviorAsset{
        .id = "tank_control",
        .module = "turret",
    });
    asset.nodes.push_back(std::move(node));

    const auto result = wz::engine::assets::instantiate_scene(asset);

    EXPECT_EQ(
        result.error,
        wz::engine::assets::SceneInstantiateError::
            DuplicateBehaviorBindingId);
    EXPECT_NE(
        result.error_detail.find("tank_control"),
        std::string::npos);
}

TEST(BehaviorDispatch, BehaviorBindingIdAffectsSceneFingerprint)
{
    wz::engine::assets::SceneAssetData a{};
    a.name = "behavior_fingerprint";
    wz::engine::assets::SceneNodeAsset node_a{};
    node_a.id = "actor";
    node_a.behavior = wz::engine::assets::SceneBehaviorAsset{
        .id = "binding_a",
        .module = "tank",
    };
    a.nodes.push_back(std::move(node_a));

    wz::engine::assets::SceneAssetData b = a;
    ASSERT_TRUE(b.nodes[0].behavior.has_value());
    b.nodes[0].behavior->id = "binding_b";

    EXPECT_NE(
        wz::engine::assets::scene_asset_fingerprint(a),
        wz::engine::assets::scene_asset_fingerprint(b));
}

TEST(BehaviorDispatch, BehaviorStateStorageAllocatesAndFindsAlignedBlocks)
{
    wz::engine::assets::BehaviorStateStorage storage{};

    auto* block = storage.allocate_instance_state(
        "actor/behavior/0",
        64u,
        32u);

    ASSERT_NE(block, nullptr);
    ASSERT_NE(block->data, nullptr);
    EXPECT_EQ(block->size, 64u);
    EXPECT_GE(block->alignment, 32u);
    EXPECT_EQ(
        reinterpret_cast<std::uintptr_t>(block->data) % block->alignment,
        0u);

    auto* found = storage.find_instance_state("actor/behavior/0");
    EXPECT_EQ(found, block);

    auto* shared = storage.allocate_shared_state("tank_group", 16u, 16u);
    ASSERT_NE(shared, nullptr);
    EXPECT_EQ(storage.find_shared_state("tank_group"), shared);

    storage.instance_state.clear();
    EXPECT_EQ(storage.find_instance_state("actor/behavior/0"), nullptr);
}

