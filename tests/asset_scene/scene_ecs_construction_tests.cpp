#include "scene_asset_module_test_support.h"

TEST(SceneECSConstruction, MakeSceneNodeSetsCoreDefaults)
{
    using namespace wz::engine::assets;

    SceneNodeAsset node = make_scene_node("root");

    EXPECT_EQ(node.id, "root");
    EXPECT_EQ(node.name, "root");
    EXPECT_FALSE(node.parent_id.has_value());
    EXPECT_TRUE(node.visible);
    EXPECT_EQ(node.motion_type,
        wz::scene::TransformNode::MotionType::Static);
    EXPECT_FLOAT_EQ(node.local.translation[0], 0.0f);
    EXPECT_FLOAT_EQ(node.local.translation[1], 0.0f);
    EXPECT_FLOAT_EQ(node.local.translation[2], 0.0f);
    EXPECT_FLOAT_EQ(node.local.rotation_quat[3], 1.0f);
    EXPECT_FLOAT_EQ(node.local.scale[0], 1.0f);
    EXPECT_FLOAT_EQ(node.local.scale[1], 1.0f);
    EXPECT_FLOAT_EQ(node.local.scale[2], 1.0f);

    SceneNodeAsset named = make_scene_node("camera", "Main Camera");
    EXPECT_EQ(named.id, "camera");
    EXPECT_EQ(named.name, "Main Camera");
}

TEST(SceneECSConstruction, AddSceneNodeAppendsAndReturnsInsertedRecord)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "construction";

    SceneNodeAsset& root = add_scene_node(scene, make_scene_node("root"));
    ASSERT_EQ(scene.nodes.size(), 1u);
    EXPECT_EQ(&root, &scene.nodes.back());
    EXPECT_EQ(root.id, "root");

    SceneNodeAsset child = make_scene_node("child");
    set_parent(child, "root");
    SceneNodeAsset& inserted = add_scene_node(scene, std::move(child));

    ASSERT_EQ(scene.nodes.size(), 2u);
    EXPECT_EQ(&inserted, &scene.nodes.back());
    EXPECT_EQ(inserted.id, "child");
    ASSERT_TRUE(inserted.parent_id.has_value());
    EXPECT_EQ(*inserted.parent_id, "root");
}

TEST(SceneECSConstruction, AttachHelpersSetAuthoredComponentsAndSummary)
{
    using namespace wz::engine::assets;
    using Kind = wz::scene::SceneAuthoredComponentKind;

    SceneAssetData scene{};
    scene.name = "helper_components";

    SceneNodeAsset node = make_scene_node("entity");

    AuthoredTransform transform{};
    transform.translation[0] = 1.0f;
    transform.translation[1] = 2.0f;
    transform.translation[2] = 3.0f;
    set_transform(node, transform);

    wz::asset::AssetKey renderable_key{};
    renderable_key.content_hash = { 0x60, 0x04 };
    wz::asset::AssetKey collision_key{};
    collision_key.content_hash = { 0xc0, 0x11 };
    attach_renderable_asset(node, renderable_key);
    attach_collision(node, SceneCollisionAsset{
        .collision_asset = collision_key,
        .layer_mask = 0x2u,
    });
    attach_camera(node, SceneCameraAsset{ .fov_y = 0.75f });
    attach_auxiliary_visual(node, SceneAuxiliaryVisualAsset{
        .kind = SceneAuxiliaryVisualKind::Axes,
        .scale = 2.0f,
    });
    attach_editor_handle(node, SceneEditorHandleAsset{
        .kind = SceneEditorHandleKind::Translate,
    });
    attach_scene_import_source(node, SceneImportSourceAsset{
        .kind = SceneImportSourceKind::GLB,
        .path = "models/entity.glb",
        .import_prefix = "entity",
    });

    const auto components = authored_components_for_node(node);
    EXPECT_NE(std::find(components.begin(), components.end(), Kind::Transform),
        components.end());
    EXPECT_NE(std::find(components.begin(), components.end(), Kind::Renderable),
        components.end());
    EXPECT_NE(std::find(components.begin(), components.end(), Kind::Collision),
        components.end());
    EXPECT_NE(std::find(components.begin(), components.end(), Kind::Camera),
        components.end());
    EXPECT_NE(std::find(
            components.begin(), components.end(), Kind::AuxiliaryVisual),
        components.end());
    EXPECT_NE(std::find(
            components.begin(), components.end(), Kind::EditorHandle),
        components.end());
    EXPECT_NE(std::find(
            components.begin(), components.end(), Kind::SceneImportSource),
        components.end());

    add_scene_node(scene, std::move(node));

    const auto summary = summarize_authored_scene_components(scene);
    EXPECT_EQ(summary.nodes, 1u);
    EXPECT_EQ(summary.transforms, 1u);
    EXPECT_EQ(summary.renderables, 1u);
    EXPECT_EQ(summary.collisions, 1u);
    EXPECT_EQ(summary.cameras, 1u);
    EXPECT_EQ(summary.auxiliary_visuals, 1u);
    EXPECT_EQ(summary.editor_handles, 1u);
    EXPECT_EQ(summary.scene_import_sources, 1u);

    const auto& stored = scene.nodes.front();
    EXPECT_EQ(stored.local.translation[0], 1.0f);
    EXPECT_EQ(*stored.renderable_asset, renderable_key);
    ASSERT_TRUE(stored.collision.has_value());
    EXPECT_EQ(stored.collision->collision_asset, collision_key);
    EXPECT_EQ(stored.collision->layer_mask, 0x2u);
    ASSERT_TRUE(stored.camera.has_value());
    EXPECT_FLOAT_EQ(stored.camera->fov_y, 0.75f);
    ASSERT_TRUE(stored.debug_visual.has_value());
    EXPECT_FLOAT_EQ(stored.debug_visual->scale, 2.0f);
    ASSERT_TRUE(stored.editor_handle.has_value());
    EXPECT_EQ(stored.editor_handle->kind, SceneEditorHandleKind::Translate);
    ASSERT_TRUE(stored.scene_import_source.has_value());
    EXPECT_EQ(stored.scene_import_source->path, "models/entity.glb");
}

TEST(SceneECSConstruction, HelperCreatedParentLinksInstantiate)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "helper_parent";
    scene.defaults.active_camera_node = "child";

    add_scene_node(scene, make_scene_node("root"));

    SceneNodeAsset child = make_scene_node("child");
    set_parent(child, "root");
    attach_camera(child);
    add_scene_node(scene, std::move(child));

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const auto& inst = result.instance;
    ASSERT_TRUE(inst.authored_to_runtime.contains("root"));
    ASSERT_TRUE(inst.authored_to_runtime.contains("child"));

    const auto root_h = inst.authored_to_runtime.at("root");
    const auto child_h = inst.authored_to_runtime.at("child");

    ASSERT_LT(root_h, inst.runtime_to_authored.size());
    ASSERT_LT(child_h, inst.runtime_to_authored.size());
    EXPECT_EQ(inst.runtime_to_authored[root_h], "root");
    EXPECT_EQ(inst.runtime_to_authored[child_h], "child");
    EXPECT_NE(inst.default_view.projection.m[0], 0.0f);
}

TEST(SceneECSConstruction, HelperCreatedRenderableAssetUsesResolverPath)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_helper_renderable_asset_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "debug/cube",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const auto renderable = assets.renderables().create_mesh_wireframe({
        .name = "debug/cube_wireframe",
        .mesh = mesh,
    });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    SceneAssetData scene{};
    scene.name = "helper_renderable_asset";

    SceneNodeAsset node = make_scene_node("cube");
    attach_renderable_asset(node, renderable.output);
    add_scene_node(scene, std::move(node));

    constexpr wz::scene::MeshHandle expected_mesh = 13;
    constexpr wz::scene::MaterialHandle expected_material = 5;

    TestRenderableResolver renderable_resolver(assets.renderables());
    TestRenderResourceResolver resource_resolver(
        expected_mesh, expected_material);

    SceneInstantiateContext context{
        .renderable_resolver = &renderable_resolver,
        .resource_resolver = &resource_resolver,
    };

    auto result = instantiate_scene(scene, context);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const auto cube_h = result.instance.authored_to_runtime.at("cube");
    ASSERT_LT(cube_h, result.instance.renderables.size());
    EXPECT_EQ(result.instance.renderables[cube_h].mesh, expected_mesh);
    EXPECT_EQ(result.instance.renderables[cube_h].material, expected_material);
}

TEST(SceneECSConstruction, HelperCreatedDuplicateIdsAreStillRejected)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "helper_duplicate_ids";

    add_scene_node(scene, make_scene_node("dup"));
    add_scene_node(scene, make_scene_node("dup"));

    auto result = instantiate_scene(scene);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, SceneInstantiateError::DuplicateNodeId);
    EXPECT_NE(result.error_detail.find("id='dup'"), std::string::npos);
    EXPECT_NE(result.error_detail.find("name='dup'"), std::string::npos);
}

