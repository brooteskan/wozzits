#include "scene_asset_module_test_support.h"

TEST(SceneAssetModule, ResolvesSceneFromJSON)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_asset_resolve_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    auto rel_path = write_scene_json(root, "test_scene.json", kSingleNodeSceneJSON);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "test_scene",
            .path = rel_path,
            });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok()) << "resolve failures: " << report.failures.size();

    const auto handle = assets.scenes().get_scene(scene_asset);
    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(handle.handle.type, kAssetTypeScene);

    const auto* data = assets.scenes().get_scene_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->name, "single_object_scene");
    EXPECT_EQ(data->nodes.size(), 2u);
    EXPECT_EQ(data->nodes[0].id, "root");
    EXPECT_EQ(data->nodes[1].id, "debug_object");
    EXPECT_TRUE(data->nodes[1].renderable.has_value());
}

TEST(SceneAssetModule, ResolvesSceneFromGLTFHierarchy)
{
    const char* gltf = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [
    { "name": "tank_scene", "nodes": [0] }
  ],
  "nodes": [
    {
      "name": "tank_body",
      "translation": [1, 0, 2],
      "children": [1]
    },
    {
      "name": "turret",
      "translation": [0, 3, 0]
    }
  ]
})";

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_gltf_hierarchy_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const auto rel_path =
        write_scene_json(root, "tank_hierarchy.gltf", gltf);

    const auto scene_asset =
        assets.scenes().create_scene_from_glb({
            .name = "tank_import",
            .path = rel_path,
        });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok()) << "resolve failures: " << report.failures.size();

    const auto* data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(data, nullptr);

    ASSERT_EQ(data->nodes.size(), 2u);
    EXPECT_EQ(data->name, "tank_scene");

    const auto* body = find_scene_node(*data, "tank_body");
    ASSERT_NE(body, nullptr);
    EXPECT_FALSE(body->parent_id.has_value());
    EXPECT_FLOAT_EQ(body->local.translation[0], 1.0f);
    EXPECT_FLOAT_EQ(body->local.translation[2], 2.0f);

    const auto* turret = find_scene_node(*data, "turret");
    ASSERT_NE(turret, nullptr);
    ASSERT_TRUE(turret->parent_id.has_value());
    EXPECT_EQ(*turret->parent_id, "tank_body");
    EXPECT_FLOAT_EQ(turret->local.translation[1], 3.0f);
}

TEST(SceneAssetModule, ResolvesGLBSceneMeshAsRenderableAsset)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        WZ_TEST_FIXTURE_DIR };

    using namespace wz::engine::assets;

    const auto scene_asset =
        assets.scenes().create_scene_from_glb({
            .name = "cube_import",
            .path = "gltf/cube.glb",
        });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok()) << "resolve failures: " << report.failures.size();

    const auto* data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(data, nullptr);

    ASSERT_EQ(data->nodes.size(), 1u);
    EXPECT_EQ(data->nodes[0].id, "Cube");
    ASSERT_TRUE(data->nodes[0].renderable_asset.has_value());
    EXPECT_FALSE(*data->nodes[0].renderable_asset == wz::asset::AssetKey{});
    EXPECT_FALSE(data->nodes[0].renderable.has_value());
}

TEST(SceneAssetModule, ResolvesTankGLBHierarchyFixture)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        WZ_TEST_FIXTURE_DIR };

    using namespace wz::engine::assets;

    const auto scene_asset =
        assets.scenes().create_scene_from_glb({
            .name = "tank1_import",
            .path = "gltf/tank1.glb",
        });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok()) << "resolve failures: " << report.failures.size();

    const auto* data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(data, nullptr);

    ASSERT_EQ(data->nodes.size(), 3u);

    const auto* body = find_scene_node(*data, "body");
    ASSERT_NE(body, nullptr);
    EXPECT_FALSE(body->parent_id.has_value());
    ASSERT_TRUE(body->renderable_asset.has_value());
    EXPECT_FALSE(*body->renderable_asset == wz::asset::AssetKey{});

    const auto* turret = find_scene_node(*data, "turret");
    ASSERT_NE(turret, nullptr);
    ASSERT_TRUE(turret->parent_id.has_value());
    EXPECT_EQ(*turret->parent_id, "body");
    ASSERT_TRUE(turret->renderable_asset.has_value());
    EXPECT_FALSE(*turret->renderable_asset == wz::asset::AssetKey{});
    EXPECT_NE(*turret->renderable_asset, *body->renderable_asset);

    const auto* gun = find_scene_node(*data, "gun");
    ASSERT_NE(gun, nullptr);
    ASSERT_TRUE(gun->parent_id.has_value());
    EXPECT_EQ(*gun->parent_id, "turret");
    ASSERT_TRUE(gun->renderable_asset.has_value());
    EXPECT_FALSE(*gun->renderable_asset == wz::asset::AssetKey{});
    EXPECT_NE(*gun->renderable_asset, *turret->renderable_asset);

    EXPECT_FLOAT_EQ(body->local.scale[0], 1.6399999856948853f);
    EXPECT_FLOAT_EQ(turret->local.translation[1], 1.6363635063171387f);
    EXPECT_FLOAT_EQ(gun->local.rotation_quat[2], 0.5323767066001892f);
}

