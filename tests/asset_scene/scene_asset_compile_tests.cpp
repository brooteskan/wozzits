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

namespace
{
    // Resolve the mesh-render style a GLB scene node's renderable was built with.
    const wz::engine::assets::MeshRenderStyleData* node_style(
        wz::engine::assets::EngineAssetLibrary& assets,
        const wz::engine::assets::SceneAssetData& scene,
        const std::string& node_id)
    {
        using namespace wz::engine::assets;
        const SceneNodeAsset* node = find_scene_node(scene, node_id);
        if (!node || !node->renderable_asset.has_value()) return nullptr;
        const RenderableAsset asset{ .output = *node->renderable_asset };
        const auto handle = assets.renderables().get_renderable(asset);
        if (!handle.valid()) return nullptr;
        const RenderableAssetData* data =
            assets.renderables().get_renderable_data(handle);
        return data ? &data->mesh_style : nullptr;
    }
}

// Issue #213 (Phase 1): per-component style mapping. Two distinct overrides on
// two different mesh indices produce renderables whose styles differ from each
// other and from the base; an un-overridden mesh keeps the base style. tank1.glb
// has three distinct meshes (body/turret/gun -> indices 0,1,2); override 0 and 1.
TEST(SceneAssetModule, GLBPerComponentStyleOverrides)
{
    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, WZ_TEST_FIXTURE_DIR };

    using namespace wz::engine::assets;

    // Base style: surface disabled, recognizable green. Each override flips
    // surface on with a distinct color so the resolved style is identifiable.
    MeshRenderStyleData base{};
    base.surface.enabled = false;

    MeshRenderStyleData style_a{};
    style_a.surface.enabled = true;
    style_a.surface.color[0] = 0.90f;  // reddish
    style_a.surface.color[1] = 0.10f;
    style_a.surface.color[2] = 0.10f;

    MeshRenderStyleData style_b{};
    style_b.surface.enabled = true;
    style_b.surface.color[0] = 0.10f;
    style_b.surface.color[1] = 0.10f;
    style_b.surface.color[2] = 0.90f;  // bluish

    const auto scene_asset = assets.scenes().create_scene_from_glb({
        .name = "tank1_styled",
        .path = "gltf/tank1.glb",
        .base_style = base,
        .style_overrides = {
            { .mesh_index = 0u, .style = style_a },
            { .mesh_index = 1u, .style = style_b },
        },
    });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok()) << "resolve failures: " << report.failures.size();

    const auto* data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(data, nullptr);
    ASSERT_EQ(data->nodes.size(), 3u);

    // Renderable keys stay distinct (distinct meshes => distinct renderables).
    const auto* body = find_scene_node(*data, "body");
    const auto* turret = find_scene_node(*data, "turret");
    const auto* gun = find_scene_node(*data, "gun");
    ASSERT_NE(body, nullptr);
    ASSERT_NE(turret, nullptr);
    ASSERT_NE(gun, nullptr);
    ASSERT_TRUE(body->renderable_asset && turret->renderable_asset
        && gun->renderable_asset);
    EXPECT_NE(*body->renderable_asset, *turret->renderable_asset);
    EXPECT_NE(*turret->renderable_asset, *gun->renderable_asset);
    EXPECT_NE(*body->renderable_asset, *gun->renderable_asset);

    // Collect the three resolved styles and classify them by surface color.
    const MeshRenderStyleData* styles[3] = {
        node_style(assets, *data, "body"),
        node_style(assets, *data, "turret"),
        node_style(assets, *data, "gun"),
    };

    int count_a = 0, count_b = 0, count_base = 0;
    for (const MeshRenderStyleData* s : styles) {
        ASSERT_NE(s, nullptr);
        if (s->surface.enabled && s->surface.color[0] > 0.5f) {
            ++count_a;  // reddish => style_a (mesh 0)
        }
        else if (s->surface.enabled && s->surface.color[2] > 0.5f) {
            ++count_b;  // bluish => style_b (mesh 1)
        }
        else if (!s->surface.enabled) {
            ++count_base;  // surface off => base (un-overridden)
        }
    }

    // Exactly the two overrides appear (distinct), and the remaining mesh keeps
    // the base. If tank1's mesh indices were not {0,1,...}, these would fail.
    EXPECT_EQ(count_a, 1) << "style override for mesh 0 must apply to one mesh";
    EXPECT_EQ(count_b, 1) << "style override for mesh 1 must apply to one mesh";
    EXPECT_EQ(count_base, 1) << "un-overridden mesh must keep the base style";
}

// Issue #213 (Phase 1): the no-override default reproduces prior behavior — a
// single uniform style across all meshes (a valid scene, no regression).
TEST(SceneAssetModule, GLBNoOverrideDefaultIsUniform)
{
    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, WZ_TEST_FIXTURE_DIR };

    using namespace wz::engine::assets;

    const auto scene_asset = assets.scenes().create_scene_from_glb({
        .name = "tank1_uniform",
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

    // Every mesh resolves to the same (default) style: the built-in
    // MeshRenderStyleData has surface disabled and wireframe enabled.
    for (const char* id : { "body", "turret", "gun" }) {
        const MeshRenderStyleData* s = node_style(assets, *data, id);
        ASSERT_NE(s, nullptr) << "node " << id << " has no resolved style";
        EXPECT_FALSE(s->surface.enabled);
        EXPECT_TRUE(s->wireframe.enabled);
    }
}

