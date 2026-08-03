#include "scene_asset_module_test_support.h"

TEST(SceneAssetModule, RenderableAssetReferenceRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_renderable_asset_reference_json_test");

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

    const auto renderable = create_test_preview_renderable(assets, "debug/cube_wireframe");
    ASSERT_TRUE(renderable.valid());

    SceneAssetData authored{};
    authored.name = "renderable_asset_reference_scene";

    SceneNodeAsset node{};
    node.id = "cube";
    node.renderable_asset = renderable.output;
    authored.nodes.push_back(std::move(node));

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(authored));
    EXPECT_NE(exported.find("\"renderable\""), std::string::npos);
    EXPECT_NE(exported.find("\"asset\""), std::string::npos);
    EXPECT_NE(exported.find("asset-key:"), std::string::npos);
    EXPECT_EQ(exported.find("\"debug_renderable\""), std::string::npos);

    auto rel_path = write_scene_json(
        root,
        "renderable_asset_reference.scene.json",
        exported);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "renderable_asset_reference",
            .path = rel_path,
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);

    const auto& parsed_node = scene_data->nodes[0];
    EXPECT_FALSE(parsed_node.renderable.has_value());
    ASSERT_TRUE(parsed_node.renderable_asset.has_value());
    EXPECT_EQ(*parsed_node.renderable_asset, renderable.output);

    const auto summary = summarize_authored_scene_components(*scene_data);
    EXPECT_EQ(summary.renderables, 1u);

    TestRenderableResolver resolver(assets.renderables());
    TestRenderResourceResolver resource_resolver(41, 7);
    SceneInstantiateContext context{
        .renderable_resolver = &resolver,
        .resource_resolver = &resource_resolver,
    };

    auto result = instantiate_scene(*scene_data, context);
    ASSERT_TRUE(result.ok()) << "error: " << result.error_detail;

    ASSERT_TRUE(result.instance.authored_to_runtime.contains("cube"));
    const auto cube_h = result.instance.authored_to_runtime["cube"];
    EXPECT_EQ(
        result.instance.renderables[cube_h].node_class.role,
        wz::scene::SceneRole::Renderable);
}

TEST(SceneAssetModule, SymbolicRenderableReferenceResolvesDuringSceneCompile)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_symbolic_renderable_ref_test");

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

    const auto renderable = create_test_preview_renderable(assets, "debug/cube_wireframe");
    ASSERT_TRUE(renderable.valid());

    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "symbolic_renderable_reference_scene",
  "nodes": [{
    "id": "cube",
    "renderable": {
      "asset": "asset://renderables/debug_cube_wireframe"
    }
  }]
})";

    auto rel_path = write_scene_json(
        root,
        "symbolic_renderable_reference.scene.json",
        json);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "symbolic_renderable_reference",
            .path = rel_path,
            .renderable_asset_references = {
                SceneAssetReferenceBinding{
                    .uri = "asset://renderables/debug_cube_wireframe",
                    .key = renderable.output,
                },
            },
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    const auto resolved_scene = assets.system().resolve(scene_asset.output);
    ASSERT_TRUE(std::holds_alternative<wz::asset::ResourceHandle>(
        resolved_scene));

    // Resolving the scene must also resolve the referenced renderable through
    // the asset DAG, not through editor-side component materialization state.
    EXPECT_TRUE(assets.renderables().get_renderable(renderable).valid());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);

    const auto& parsed_node = scene_data->nodes[0];
    EXPECT_FALSE(parsed_node.renderable.has_value());
    ASSERT_TRUE(parsed_node.renderable_asset.has_value());
    EXPECT_EQ(*parsed_node.renderable_asset, renderable.output);

    TestRenderableResolver resolver(assets.renderables());
    TestRenderResourceResolver resource_resolver(41, 7);
    SceneInstantiateContext context{
        .renderable_resolver = &resolver,
        .resource_resolver = &resource_resolver,
    };

    auto result = instantiate_scene(*scene_data, context);
    ASSERT_TRUE(result.ok()) << "error: " << result.error_detail;
    ASSERT_TRUE(result.instance.authored_to_runtime.contains("cube"));

    const auto cube_h = result.instance.authored_to_runtime["cube"];
    EXPECT_EQ(
        result.instance.renderables[cube_h].node_class.role,
        wz::scene::SceneRole::Renderable);
}

TEST(SceneAssetModule, SceneCanReferenceRenderableRegisteredAfterInitialCommit)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_incremental_renderable_ref_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    ASSERT_TRUE(assets.commit());

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "debug/incremental_cube",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const auto renderable = create_test_preview_renderable(assets, "debug/incremental_cube_wireframe");
    ASSERT_TRUE(renderable.valid());

    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "incremental_symbolic_renderable_reference_scene",
  "nodes": [{
    "id": "cube",
    "renderable": {
      "asset": "asset://renderables/incremental_cube_wireframe"
    }
  }]
})";

    auto rel_path = write_scene_json(
        root,
        "incremental_symbolic_renderable_reference.scene.json",
        json);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "incremental_symbolic_renderable_reference",
            .path = rel_path,
            .renderable_asset_references = {
                SceneAssetReferenceBinding{
                    .uri = "asset://renderables/incremental_cube_wireframe",
                    .key = renderable.output,
                },
            },
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());

    const auto resolved_scene = assets.system().resolve(scene_asset.output);
    ASSERT_TRUE(std::holds_alternative<wz::asset::ResourceHandle>(
        resolved_scene));
    EXPECT_TRUE(assets.renderables().get_renderable(renderable).valid());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);

    TestRenderableResolver resolver(assets.renderables());
    TestRenderResourceResolver resource_resolver(41, 7);
    SceneInstantiateContext context{
        .renderable_resolver = &resolver,
        .resource_resolver = &resource_resolver,
    };
    auto result = instantiate_scene(*scene_data, context);
    ASSERT_TRUE(result.ok()) << "error: " << result.error_detail;
    ASSERT_TRUE(result.instance.authored_to_runtime.contains("cube"));
}

TEST(SceneAssetModule, MissingSymbolicRenderableReferenceFailsSceneCompile)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_missing_symbolic_renderable_ref_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "missing_symbolic_renderable_reference_scene",
  "nodes": [{
    "id": "cube",
    "renderable": {
      "asset": "asset://renderables/missing"
    }
  }]
})";

    auto rel_path = write_scene_json(
        root,
        "missing_symbolic_renderable_reference.scene.json",
        json);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "missing_symbolic_renderable_reference",
            .path = rel_path,
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    EXPECT_FALSE(report.ok());
    EXPECT_FALSE(report.failures.empty());

    const auto handle = assets.scenes().get_scene(scene_asset);
    EXPECT_FALSE(handle.valid());
}

TEST(SceneAssetModule, PersistsEditorHandleTranslationEdit)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "persist_translation";

    SceneNodeAsset node{};
    node.id = "rock";
    node.editor_handle = SceneEditorHandleAsset{
        .kind = SceneEditorHandleKind::Transform,
    };
    scene.nodes.push_back(std::move(node));

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << "error: " << result.error_detail;

    auto& inst = result.instance;
    auto rock_h = inst.authored_to_runtime["rock"];

    AuthoredTransform edited{};
    edited.translation[0] = 3.0f;
    edited.translation[1] = 4.0f;
    edited.translation[2] = 5.0f;

    wz::scene::set_local(
        inst.storage.polytree,
        rock_h,
        compose_scene_transform(edited));
    wz::scene::propagate_all(inst.storage.polytree);

    ASSERT_TRUE(update_scene_asset_node_transform(
        scene,
        inst,
        rock_h,
        edited));

    EXPECT_FLOAT_EQ(scene.nodes[0].local.translation[0], 3.0f);
    EXPECT_FLOAT_EQ(scene.nodes[0].local.translation[1], 4.0f);
    EXPECT_FLOAT_EQ(scene.nodes[0].local.translation[2], 5.0f);

    const auto& runtime = wz::core::graph::node_data(
        inst.storage.polytree,
        rock_h);
    EXPECT_FLOAT_EQ(runtime.local.m[12], 3.0f);
    EXPECT_FLOAT_EQ(runtime.local.m[13], 4.0f);
    EXPECT_FLOAT_EQ(runtime.local.m[14], 5.0f);

    auto reloaded = instantiate_scene(scene);
    ASSERT_TRUE(reloaded.ok()) << "error: " << reloaded.error_detail;

    auto reloaded_h = reloaded.instance.authored_to_runtime["rock"];
    const auto& reloaded_node = wz::core::graph::node_data(
        reloaded.instance.storage.polytree,
        reloaded_h);
    EXPECT_FLOAT_EQ(reloaded_node.local.m[12], 3.0f);
    EXPECT_FLOAT_EQ(reloaded_node.local.m[13], 4.0f);
    EXPECT_FLOAT_EQ(reloaded_node.local.m[14], 5.0f);
}

TEST(SceneAssetModule, PersistsEditorHandleRotationEdit)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "persist_rotation";

    SceneNodeAsset node{};
    node.id = "rock";
    node.editor_handle = SceneEditorHandleAsset{
        .kind = SceneEditorHandleKind::Transform,
    };
    scene.nodes.push_back(std::move(node));

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << "error: " << result.error_detail;

    auto& inst = result.instance;
    auto rock_h = inst.authored_to_runtime["rock"];

    constexpr float kPi = 3.14159265358979323846f;
    const wz::math::Quaternion q =
        wz::math::from_axis_angle({ 0.0f, 1.0f, 0.0f }, kPi * 0.5f);

    AuthoredTransform edited{};
    edited.rotation_quat[0] = q.x;
    edited.rotation_quat[1] = q.y;
    edited.rotation_quat[2] = q.z;
    edited.rotation_quat[3] = q.w;

    wz::scene::set_local(
        inst.storage.polytree,
        rock_h,
        compose_scene_transform(edited));
    wz::scene::propagate_all(inst.storage.polytree);

    ASSERT_TRUE(update_scene_asset_node_transform(
        scene,
        inst,
        "rock",
        edited));

    EXPECT_NEAR(scene.nodes[0].local.rotation_quat[0], q.x, 1e-5f);
    EXPECT_NEAR(scene.nodes[0].local.rotation_quat[1], q.y, 1e-5f);
    EXPECT_NEAR(scene.nodes[0].local.rotation_quat[2], q.z, 1e-5f);
    EXPECT_NEAR(scene.nodes[0].local.rotation_quat[3], q.w, 1e-5f);

    const auto& runtime = wz::core::graph::node_data(
        inst.storage.polytree,
        rock_h);
    EXPECT_NEAR(runtime.local.m[0], 0.0f, 1e-5f);
    EXPECT_NEAR(runtime.local.m[2], -1.0f, 1e-5f);
    EXPECT_NEAR(runtime.local.m[8], 1.0f, 1e-5f);
    EXPECT_NEAR(runtime.local.m[10], 0.0f, 1e-5f);

    auto reloaded = instantiate_scene(scene);
    ASSERT_TRUE(reloaded.ok()) << "error: " << reloaded.error_detail;

    auto reloaded_h = reloaded.instance.authored_to_runtime["rock"];
    const auto& reloaded_node = wz::core::graph::node_data(
        reloaded.instance.storage.polytree,
        reloaded_h);
    EXPECT_NEAR(reloaded_node.local.m[0], 0.0f, 1e-5f);
    EXPECT_NEAR(reloaded_node.local.m[2], -1.0f, 1e-5f);
    EXPECT_NEAR(reloaded_node.local.m[8], 1.0f, 1e-5f);
    EXPECT_NEAR(reloaded_node.local.m[10], 0.0f, 1e-5f);
}

TEST(SceneAssetModule, PersistedChildTransformPreservesParentRelationship)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "persist_child";

    SceneNodeAsset root{};
    root.id = "root";
    root.local.translation[0] = 10.0f;
    scene.nodes.push_back(std::move(root));

    SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "root";
    child.editor_handle = SceneEditorHandleAsset{
        .kind = SceneEditorHandleKind::Transform,
    };
    scene.nodes.push_back(std::move(child));

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << "error: " << result.error_detail;

    auto& inst = result.instance;
    auto root_h = inst.authored_to_runtime["root"];
    auto child_h = inst.authored_to_runtime["child"];

    constexpr float kPi = 3.14159265358979323846f;
    const wz::math::Quaternion q =
        wz::math::from_axis_angle({ 0.0f, 1.0f, 0.0f }, kPi * 0.5f);

    AuthoredTransform edited{};
    edited.translation[0] = 2.0f;
    edited.translation[1] = 3.0f;
    edited.translation[2] = 4.0f;
    edited.rotation_quat[0] = q.x;
    edited.rotation_quat[1] = q.y;
    edited.rotation_quat[2] = q.z;
    edited.rotation_quat[3] = q.w;

    wz::scene::set_local(
        inst.storage.polytree,
        child_h,
        compose_scene_transform(edited));
    wz::scene::propagate_all(inst.storage.polytree);

    ASSERT_TRUE(update_scene_asset_node_transform(
        scene,
        inst,
        child_h,
        edited));

    EXPECT_FLOAT_EQ(scene.nodes[0].local.translation[0], 10.0f);
    EXPECT_FLOAT_EQ(scene.nodes[1].local.translation[0], 2.0f);
    EXPECT_FLOAT_EQ(scene.nodes[1].local.translation[1], 3.0f);
    EXPECT_FLOAT_EQ(scene.nodes[1].local.translation[2], 4.0f);

    const auto& root_world = wz::core::graph::node_data(
        inst.storage.polytree,
        root_h).world;
    const auto& child_world = wz::core::graph::node_data(
        inst.storage.polytree,
        child_h).world;

    EXPECT_FLOAT_EQ(root_world.m[12], 10.0f);
    EXPECT_FLOAT_EQ(child_world.m[12], 12.0f);
    EXPECT_FLOAT_EQ(child_world.m[13], 3.0f);
    EXPECT_FLOAT_EQ(child_world.m[14], 4.0f);

    auto reloaded = instantiate_scene(scene);
    ASSERT_TRUE(reloaded.ok()) << "error: " << reloaded.error_detail;

    auto reloaded_child_h = reloaded.instance.authored_to_runtime["child"];
    const auto& reloaded_child = wz::core::graph::node_data(
        reloaded.instance.storage.polytree,
        reloaded_child_h);
    EXPECT_FLOAT_EQ(reloaded_child.world.m[12], 12.0f);
    EXPECT_FLOAT_EQ(reloaded_child.world.m[13], 3.0f);
    EXPECT_FLOAT_EQ(reloaded_child.world.m[14], 4.0f);
    EXPECT_NEAR(reloaded_child.local.m[0], 0.0f, 1e-5f);
    EXPECT_NEAR(reloaded_child.local.m[2], -1.0f, 1e-5f);
}

TEST(SceneAssetModule, ExportedSceneJSONReloadsEditedTransforms)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "export_transform_edits";

    SceneNodeAsset root{};
    root.id = "root";
    root.local.translation[0] = 10.0f;
    scene.nodes.push_back(std::move(root));

    SceneNodeAsset child{};
    child.id = "rock";
    child.parent_id = "root";
    child.editor_handle = SceneEditorHandleAsset{
        .kind = SceneEditorHandleKind::Transform,
    };
    scene.nodes.push_back(std::move(child));

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << "error: " << result.error_detail;

    auto& inst = result.instance;
    auto rock_h = inst.authored_to_runtime["rock"];

    constexpr float kPi = 3.14159265358979323846f;
    const wz::math::Quaternion q =
        wz::math::from_axis_angle({ 0.0f, 1.0f, 0.0f }, kPi * 0.5f);

    AuthoredTransform edited{};
    edited.translation[0] = 2.0f;
    edited.translation[1] = 3.0f;
    edited.translation[2] = 4.0f;
    edited.rotation_quat[0] = q.x;
    edited.rotation_quat[1] = q.y;
    edited.rotation_quat[2] = q.z;
    edited.rotation_quat[3] = q.w;

    ASSERT_TRUE(update_scene_asset_node_transform(scene, inst, rock_h, edited));

    const std::string json_text =
        wz::json::serialize_json(export_scene_to_json_document(scene));

    const wz::fs::Path root_path =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_export_reload_test");
    ASSERT_EQ(wz::fs::create_directories(root_path), wz::fs::FileError::None);

    auto rel_path = write_scene_json(
        root_path,
        "exported_scene.json",
        json_text);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root_path };

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "exported_scene",
        .path = rel_path,
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto handle = assets.scenes().get_scene(scene_asset);
    ASSERT_TRUE(handle.valid());

    const auto* reloaded_scene = assets.scenes().get_scene_data(handle);
    ASSERT_NE(reloaded_scene, nullptr);

    auto reloaded = instantiate_scene(*reloaded_scene);
    ASSERT_TRUE(reloaded.ok()) << "error: " << reloaded.error_detail;

    auto reloaded_h = reloaded.instance.authored_to_runtime["rock"];
    const auto& reloaded_node = wz::core::graph::node_data(
        reloaded.instance.storage.polytree,
        reloaded_h);

    EXPECT_FLOAT_EQ(reloaded_node.world.m[12], 12.0f);
    EXPECT_FLOAT_EQ(reloaded_node.world.m[13], 3.0f);
    EXPECT_FLOAT_EQ(reloaded_node.world.m[14], 4.0f);
    EXPECT_NEAR(reloaded_node.local.m[0], 0.0f, 1e-5f);
    EXPECT_NEAR(reloaded_node.local.m[2], -1.0f, 1e-5f);
    EXPECT_NEAR(reloaded_node.local.m[8], 1.0f, 1e-5f);
    EXPECT_NEAR(reloaded_node.local.m[10], 0.0f, 1e-5f);

    ASSERT_EQ(reloaded_scene->nodes.size(), 2u);
    ASSERT_TRUE(reloaded_scene->nodes[1].editor_handle.has_value());
    EXPECT_EQ(reloaded_scene->nodes[1].editor_handle->kind,
        SceneEditorHandleKind::Transform);
}

TEST(SceneAssetModule, RenderableAssetDrivesTheSceneCompilePipeline)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_renderable_asset_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    // Create a real renderable asset through the normal path
    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "debug/cube",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const auto renderable = create_test_preview_renderable(assets, "debug/cube_wireframe");
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    // Verify the renderable resolved
    const auto rhandle = assets.renderables().get_renderable(renderable);
    ASSERT_TRUE(rhandle.valid());
    const auto* rdata = assets.renderables().get_renderable_data(rhandle);
    ASSERT_NE(rdata, nullptr);
    EXPECT_EQ(rdata->kind, RenderableKind::ScalarField);

    // Build a SceneAssetData in memory that references the renderable
    SceneAssetData scene{};
    scene.name = "renderable_asset_scene";

    SceneNodeAsset node{};
    node.id = "cube_node";
    node.local.translation[0] = 2.0f;
    node.local.translation[1] = 0.0f;
    node.local.translation[2] = 3.0f;
    node.renderable_asset = renderable.output;
    scene.nodes.push_back(std::move(node));

    // Instantiate with resolver
    TestRenderableResolver resolver(assets.renderables());
    TestRenderResourceResolver resource_resolver(41, 7);
    SceneInstantiateContext context{
        .renderable_resolver = &resolver,
        .resource_resolver = &resource_resolver,
    };

    auto result = instantiate_scene(scene, context);
    ASSERT_TRUE(result.ok()) << "error: " << result.error_detail;

    auto& inst = result.instance;

    // Validate scene structure
    EXPECT_EQ(wz::core::graph::node_count(inst.storage.polytree), 1u);
    EXPECT_TRUE(inst.authored_to_runtime.contains("cube_node"));
    auto cube_h = inst.authored_to_runtime["cube_node"];

    // Renderable descriptor should be filled from the resolved asset
    const auto& desc = inst.renderables[cube_h];
    EXPECT_EQ(desc.node_class.role, wz::scene::SceneRole::Renderable);
    EXPECT_EQ(desc.node_class.producer, wz::scene::ProducerKind::Mesh);
    EXPECT_EQ(desc.node_class.default_surface, wz::scene::SurfaceClass::Opaque);
    EXPECT_TRUE(desc.visible);
    EXPECT_NE(desc.mesh, wz::scene::INVALID_MESH);

    // World transform should reflect the authored translation
    const auto& world = wz::core::graph::node_data(
        inst.storage.polytree, cube_h).world;
    EXPECT_FLOAT_EQ(world.m[12], 2.0f);
    EXPECT_FLOAT_EQ(world.m[14], 3.0f);

    // Full render pipeline: compile → IR → frame
    wz::scene::ViewData view{};
    view.camera_position = { 0.f, 0.f, 0.f };
    view.view = wz::math::Mat4::identity();

    constexpr float Pi = 3.14159265358979323846f;
    const float fov = 70.0f * Pi / 180.0f;
    view.projection = wz::math::projection_perspective_dx(
        fov, 16.f / 9.f, 0.1f, 100.f);
    view.view_projection = wz::math::mul(view.projection, view.view);

    wz::scene::CompiledSceneStorage compiled{};
    wz::scene::compile(
        compiled,
        inst.storage.polytree,
        inst.renderables,
        inst.lights,
        view);

    ASSERT_EQ(compiled.scene.opaque.size(), 1u);
    EXPECT_FLOAT_EQ(compiled.scene.opaque[0].world.m[12], 2.0f);
    EXPECT_FLOAT_EQ(compiled.scene.opaque[0].world.m[14], 3.0f);

    wz::render::RenderIRStorage render_ir{};
    wz::render::build_render_ir(render_ir, compiled.scene);

    wz::render::RenderFrameStorage render_frame{};
    wz::render::build_frame(render_frame, render_ir.ir, compiled.scene);

    ASSERT_EQ(render_frame.frame.opaque.size(), 1u);

    const auto& cmd = render_frame.frame.opaque[0];
    EXPECT_FLOAT_EQ(cmd.world.m[12], 2.0f);
    EXPECT_FLOAT_EQ(cmd.world.m[14], 3.0f);
}

TEST(SceneAssetModule, RenderableAssetWithNonRenderableNode)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_renderable_mixed_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    // Registered for the side effect -- create_test_preview_renderable reaches
    // it by name -- so unlike the other tests here the handle is unused.
    (void)assets.meshes().create_procedural_mesh({
        .name = "debug/cube",
        .kind = ProceduralMeshKind::Cube,
    });
    const auto renderable = create_test_preview_renderable(assets, "debug/cube_wireframe");

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    // Scene: one renderable node, one camera-only node with debug visual
    SceneAssetData scene{};
    scene.name = "mixed_scene";

    SceneNodeAsset render_node{};
    render_node.id = "cube";
    render_node.local.translation[0] = 5.0f;
    render_node.renderable_asset = renderable.output;
    scene.nodes.push_back(std::move(render_node));

    SceneNodeAsset camera_node{};
    camera_node.id = "cam";
    camera_node.camera = SceneCameraAsset{};
    camera_node.debug_visual = SceneDebugVisualAsset{
        .kind = SceneDebugVisualKind::Axes,
        .scale = 1.0f,
        .visible = true,
    };
    scene.nodes.push_back(std::move(camera_node));

    TestRenderableResolver resolver(assets.renderables());
    TestRenderResourceResolver resource_resolver(41, 7);
    SceneInstantiateContext context{
        .renderable_resolver = &resolver,
        .resource_resolver = &resource_resolver,
    };

    auto result = instantiate_scene(scene, context);
    ASSERT_TRUE(result.ok());

    auto& inst = result.instance;

    EXPECT_EQ(wz::core::graph::node_count(inst.storage.polytree), 2u);

    // Cube node has renderable
    auto cube_h = inst.authored_to_runtime["cube"];
    EXPECT_EQ(inst.renderables[cube_h].node_class.role,
        wz::scene::SceneRole::Renderable);

    // Camera node has no renderable but has debug visual
    auto cam_h = inst.authored_to_runtime["cam"];
    EXPECT_EQ(inst.renderables[cam_h].node_class.role,
        wz::scene::SceneRole::None);
    ASSERT_EQ(inst.auxiliary_visuals.size(), 1u);
    EXPECT_EQ(inst.auxiliary_visuals[0].node, cam_h);

    // Compile: only one draw command (from the cube)
    wz::scene::ViewData view{};
    view.view = wz::math::Mat4::identity();
    view.projection = wz::math::Mat4::identity();
    view.view_projection = wz::math::Mat4::identity();

    wz::scene::CompiledSceneStorage compiled{};
    wz::scene::compile(
        compiled,
        inst.storage.polytree,
        inst.renderables,
        inst.lights,
        view);

    EXPECT_EQ(compiled.scene.opaque.size(), 1u);
}

// Issue #213 (Phase 1): a host node's scene_source (the authored asset-graph
// node id) round-trips losslessly through scene JSON. Mirrors the renderable
// asset-graph-node-id persistence: export emits "scene_source" with
// "asset_graph_node_id"; parse reads it back into scene_source_node_id.
TEST(SceneAssetModule, SceneSourceReferenceRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_source_reference_json_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    SceneAssetData authored{};
    authored.name = "scene_source_reference_scene";

    // A host node that references a "Scene from GLB" authored asset-graph node.
    SceneNodeAsset host{};
    host.id = "tank_host";
    attach_scene_source_node(host, 4242u);
    authored.nodes.push_back(std::move(host));

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(authored));
    EXPECT_NE(exported.find("\"scene_source\""), std::string::npos);
    EXPECT_NE(exported.find("\"asset_graph_node_id\""), std::string::npos);

    auto rel_path = write_scene_json(
        root,
        "scene_source_reference.scene.json",
        exported);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "scene_source_reference",
            .path = rel_path,
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);

    const auto& parsed_node = scene_data->nodes[0];
    EXPECT_EQ(parsed_node.id, "tank_host");
    ASSERT_TRUE(parsed_node.scene_source_node_id.has_value());
    EXPECT_EQ(*parsed_node.scene_source_node_id, 4242u);
    // The resolved key is re-bridged on (re)bind, not persisted.
    EXPECT_FALSE(parsed_node.scene_source.has_value());
}

// Issue #213 (Phase 1): a node WITHOUT a scene_source emits no "scene_source"
// member (additive, no spurious output) and round-trips with the slot empty.
TEST(SceneAssetModule, NoSceneSourceEmitsNoMemberAndRoundTrips)
{
    wz::Logger logger;

    using namespace wz::engine::assets;

    SceneAssetData authored{};
    authored.name = "no_scene_source_scene";

    SceneNodeAsset node{};
    node.id = "plain";
    authored.nodes.push_back(std::move(node));

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(authored));
    EXPECT_EQ(exported.find("\"scene_source\""), std::string::npos);
}

