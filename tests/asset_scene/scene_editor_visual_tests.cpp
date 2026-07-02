#include "scene_asset_module_test_support.h"

TEST(SceneAssetModule, EmptyAnchorWithAxesDebugVisual)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_debug_axes_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    auto rel_path = write_scene_json(
        root, "debug_axes.json", kDebugAxesDescriptorSceneJSON);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "debug_axes",
            .path = rel_path,
            });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);

    // Verify parsed asset data
    ASSERT_EQ(scene_data->nodes.size(), 1u);
    const auto& node = scene_data->nodes[0];
    EXPECT_EQ(node.id, "empty_anchor");
    EXPECT_FALSE(node.renderable.has_value());
    EXPECT_FALSE(node.camera.has_value());
    ASSERT_TRUE(node.debug_visual.has_value());
    EXPECT_EQ(node.debug_visual->kind, SceneDebugVisualKind::Axes);
    EXPECT_FLOAT_EQ(node.debug_visual->scale, 1.5f);
    EXPECT_TRUE(node.debug_visual->visible);

    // Instantiate
    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok());

    auto& inst = result.instance;

    // Node exists in graph
    EXPECT_EQ(wz::core::graph::node_count(inst.storage.polytree), 1u);
    EXPECT_TRUE(inst.authored_to_runtime.contains("empty_anchor"));
    auto anchor_h = inst.authored_to_runtime["empty_anchor"];

    // One debug visual record
    ASSERT_EQ(inst.auxiliary_visuals.size(), 1u);
    EXPECT_EQ(inst.auxiliary_visuals[0].node, anchor_h);
    EXPECT_EQ(inst.auxiliary_visuals[0].component.kind, SceneDebugVisualKind::Axes);
    EXPECT_FLOAT_EQ(inst.auxiliary_visuals[0].component.scale, 1.5f);
    EXPECT_TRUE(inst.auxiliary_visuals[0].component.visible);

    // No renderable
    EXPECT_EQ(inst.renderables[anchor_h].node_class.role,
        wz::scene::SceneRole::None);

    // Compile with identity view — zero render output
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

    EXPECT_EQ(compiled.scene.opaque.size(), 0u);
}

TEST(SceneAssetModule, CameraNodeWithDebugVisual)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_camera_debug_visual_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    auto rel_path = write_scene_json(
        root, "camera_debug_visual.json", kCameraWithDebugVisualSceneJSON);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "camera_debug_visual",
            .path = rel_path,
            });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);

    // Verify both camera and debug_visual are parsed
    ASSERT_EQ(scene_data->nodes.size(), 1u);
    const auto& node = scene_data->nodes[0];
    ASSERT_TRUE(node.camera.has_value());
    ASSERT_TRUE(node.debug_visual.has_value());
    EXPECT_EQ(node.debug_visual->kind, SceneDebugVisualKind::Axes);
    EXPECT_FLOAT_EQ(node.debug_visual->scale, 0.5f);

    // Instantiate
    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok());

    auto& inst = result.instance;
    auto cam_h = inst.authored_to_runtime["cam_node"];

    // Default view should be populated (camera works)
    EXPECT_NEAR(inst.default_view.camera_position.y, 5.0f, 1e-4f);
    EXPECT_NE(inst.default_view.projection.m[0], 0.0f);

    // Debug visual record exists
    ASSERT_EQ(inst.auxiliary_visuals.size(), 1u);
    EXPECT_EQ(inst.auxiliary_visuals[0].node, cam_h);
    EXPECT_EQ(inst.auxiliary_visuals[0].component.kind, SceneDebugVisualKind::Axes);
    EXPECT_FLOAT_EQ(inst.auxiliary_visuals[0].component.scale, 0.5f);

    // No renderable
    EXPECT_EQ(inst.renderables[cam_h].node_class.role,
        wz::scene::SceneRole::None);
}

TEST(SceneAssetModule, FlyCameraWithDebugVisual)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_fly_cam_debug_visual_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    auto rel_path = write_scene_json(
        root, "fly_cam_debug_visual.json", kFlyCameraWithDebugVisualSceneJSON);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "fly_cam_debug_visual",
            .path = rel_path,
            });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);

    // Verify all descriptors coexist
    ASSERT_EQ(scene_data->nodes.size(), 1u);
    const auto& node = scene_data->nodes[0];
    ASSERT_TRUE(node.camera.has_value());
    ASSERT_TRUE(node.input_receiver.has_value());
    ASSERT_TRUE(node.flying_camera_controller.has_value());
    ASSERT_TRUE(node.debug_visual.has_value());
    EXPECT_EQ(node.debug_visual->kind, SceneDebugVisualKind::Axes);
    EXPECT_FLOAT_EQ(node.debug_visual->scale, 2.0f);
    EXPECT_FALSE(node.debug_visual->visible);

    // Instantiate
    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok());

    auto& inst = result.instance;
    auto fly_h = inst.authored_to_runtime["editor_fly_cam"];

    // All component tables populated
    ASSERT_EQ(inst.input_receivers.size(), 1u);
    EXPECT_EQ(inst.input_receivers[0].node, fly_h);

    ASSERT_EQ(inst.flying_camera_controllers.size(), 1u);
    EXPECT_EQ(inst.flying_camera_controllers[0].node, fly_h);

    ASSERT_EQ(inst.auxiliary_visuals.size(), 1u);
    EXPECT_EQ(inst.auxiliary_visuals[0].node, fly_h);
    EXPECT_EQ(inst.auxiliary_visuals[0].component.kind, SceneDebugVisualKind::Axes);
    EXPECT_FLOAT_EQ(inst.auxiliary_visuals[0].component.scale, 2.0f);
    EXPECT_FALSE(inst.auxiliary_visuals[0].component.visible);

    // Camera default view still works
    EXPECT_NEAR(inst.default_view.camera_position.y, 10.0f, 1e-4f);

    // No renderable
    EXPECT_EQ(inst.renderables[fly_h].node_class.role,
        wz::scene::SceneRole::None);
}

TEST(SceneAssetModule, DebugVisualDefaultsVisibleTrue)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_debug_vis_default_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    auto rel_path = write_scene_json(
        root, "debug_vis_default.json", kDebugVisualDefaultVisibleSceneJSON);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "debug_vis_default",
            .path = rel_path,
            });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);

    ASSERT_EQ(scene_data->nodes.size(), 1u);
    ASSERT_TRUE(scene_data->nodes[0].debug_visual.has_value());
    EXPECT_TRUE(scene_data->nodes[0].debug_visual->visible);

    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok());

    ASSERT_EQ(result.instance.auxiliary_visuals.size(), 1u);
    EXPECT_TRUE(result.instance.auxiliary_visuals[0].component.visible);
}

TEST(SceneAssetModule, AuxiliaryVisualJSONSpellingCompilesToVisualComponent)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_auxiliary_visual_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    auto rel_path = write_scene_json(
        root, "auxiliary_visual.json", kAuxiliaryVisualDescriptorSceneJSON);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "auxiliary_visual",
            .path = rel_path,
            });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);

    ASSERT_EQ(scene_data->nodes.size(), 1u);
    ASSERT_TRUE(scene_data->nodes[0].debug_visual.has_value());
    EXPECT_EQ(
        scene_data->nodes[0].debug_visual->kind,
        SceneAuxiliaryVisualKind::Axes);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].debug_visual->scale, 1.25f);
    EXPECT_FALSE(scene_data->nodes[0].debug_visual->visible);

    const auto authored_summary =
        summarize_authored_scene_components(*scene_data);
    EXPECT_EQ(authored_summary.auxiliary_visuals, 1u);

    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    ASSERT_EQ(result.instance.auxiliary_visuals.size(), 1u);
    EXPECT_EQ(
        result.instance.auxiliary_visuals[0].component.kind,
        SceneAuxiliaryVisualKind::Axes);
    EXPECT_FLOAT_EQ(result.instance.auxiliary_visuals[0].component.scale, 1.25f);
    EXPECT_FALSE(result.instance.auxiliary_visuals[0].component.visible);

    const auto runtime_summary =
        summarize_scene_instance_components(result.instance);
    EXPECT_EQ(runtime_summary.auxiliary_visuals, 1u);
}

TEST(SceneDescriptorValidation, RejectsMissingDebugVisualKind)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "no_kind",
  "nodes": [{
    "id": "n",
    "debug_visual": { "scale": 1.0 }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("missing_dv_kind", json));
}

TEST(SceneDescriptorValidation, RejectsUnknownDebugVisualKind)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "bad_kind",
  "nodes": [{
    "id": "n",
    "debug_visual": { "kind": "camera_frustum" }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("unknown_dv_kind", json));
}

TEST(SceneDescriptorValidation, RejectsNegativeDebugVisualScale)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "neg_scale",
  "nodes": [{
    "id": "n",
    "debug_visual": { "kind": "axes", "scale": -1.0 }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("neg_dv_scale", json));
}

TEST(SceneDescriptorValidation, AcceptsZeroDebugVisualScale)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "zero_scale",
  "nodes": [{
    "id": "n",
    "debug_visual": { "kind": "axes", "scale": 0.0 }
  }]
})";
    EXPECT_TRUE(scene_json_compiles("zero_dv_scale", json));
}

TEST(SceneDescriptorValidation, RejectsInfiniteDebugVisualScale)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "inf_scale",
  "nodes": [{
    "id": "n",
    "debug_visual": { "kind": "axes", "scale": 1e309 }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("inf_dv_scale", json));
}

TEST(SceneDescriptorValidation, RejectsMissingEditorHandleKind)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "no_editor_kind",
  "nodes": [{
    "id": "n",
    "editor_handle": { "size": 1.0 }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("missing_editor_kind", json));
}

TEST(SceneDescriptorValidation, RejectsUnknownEditorHandleKind)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "bad_editor_kind",
  "nodes": [{
    "id": "n",
    "editor_handle": { "kind": "skew" }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("unknown_editor_kind", json));
}

TEST(SceneDescriptorValidation, RejectsNegativeEditorHandleSize)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "neg_editor_size",
  "nodes": [{
    "id": "n",
    "editor_handle": { "kind": "transform", "size": -1.0 }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("neg_editor_size", json));
}

TEST(SceneDescriptorValidation, AcceptsZeroEditorHandleSize)
{
    EXPECT_TRUE(scene_json_compiles(
        "zero_editor_size",
        kEditorHandleMixedDescriptorsSceneJSON));
}

TEST(SceneDescriptorValidation, RejectsInfiniteEditorHandleSize)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "inf_editor_size",
  "nodes": [{
    "id": "n",
    "editor_handle": { "kind": "transform", "size": 1e309 }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("inf_editor_size", json));
}

TEST(SceneAssetModule, NonRenderableAnchorNodeWithEditorHandle)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_editor_handle_anchor_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    auto rel_path = write_scene_json(
        root, "editor_handle_anchor.json", kEditorHandleAnchorSceneJSON);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "editor_handle_anchor",
            .path = rel_path,
            });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);

    ASSERT_EQ(scene_data->nodes.size(), 1u);
    const auto& node = scene_data->nodes[0];
    EXPECT_EQ(node.id, "terrain_anchor");
    EXPECT_FALSE(node.renderable.has_value());
    ASSERT_TRUE(node.debug_visual.has_value());
    ASSERT_TRUE(node.editor_handle.has_value());
    EXPECT_EQ(node.editor_handle->kind, SceneEditorHandleKind::Translate);
    EXPECT_TRUE(node.editor_handle->enabled);
    EXPECT_FALSE(node.editor_handle->visible);
    EXPECT_FLOAT_EQ(node.editor_handle->size, 2.5f);

    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << "error: " << result.error_detail;

    auto& inst = result.instance;
    ASSERT_TRUE(inst.authored_to_runtime.contains("terrain_anchor"));
    auto anchor_h = inst.authored_to_runtime["terrain_anchor"];

    EXPECT_EQ(inst.renderables[anchor_h].node_class.role,
        wz::scene::SceneRole::None);

    ASSERT_EQ(inst.auxiliary_visuals.size(), 1u);
    EXPECT_EQ(inst.auxiliary_visuals[0].node, anchor_h);

    ASSERT_EQ(inst.editor_handles.size(), 1u);
    EXPECT_EQ(inst.editor_handles[0].node, anchor_h);
    EXPECT_EQ(inst.editor_handles[0].component.kind,
        SceneEditorHandleKind::Translate);
    EXPECT_TRUE(inst.editor_handles[0].component.enabled);
    EXPECT_FALSE(inst.editor_handles[0].component.visible);
    EXPECT_FLOAT_EQ(inst.editor_handles[0].component.size, 2.5f);

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

    EXPECT_EQ(compiled.scene.opaque.size(), 0u);
}

TEST(SceneAssetModule, EditorHandleCoexistsWithCameraInputAudioAndEvents)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_editor_handle_mixed_descriptors_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    auto rel_path = write_scene_json(
        root, "editor_handle_mixed.json",
        kEditorHandleMixedDescriptorsSceneJSON);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "editor_handle_mixed",
            .path = rel_path,
            });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);

    ASSERT_EQ(scene_data->nodes.size(), 1u);
    const auto& node = scene_data->nodes[0];
    ASSERT_TRUE(node.camera.has_value());
    ASSERT_TRUE(node.input_receiver.has_value());
    ASSERT_TRUE(node.flying_camera_controller.has_value());
    ASSERT_TRUE(node.audio_listener.has_value());
    ASSERT_TRUE(node.event_listener.has_value());
    ASSERT_TRUE(node.editor_handle.has_value());
    EXPECT_EQ(node.editor_handle->kind, SceneEditorHandleKind::Transform);
    EXPECT_TRUE(node.editor_handle->enabled);
    EXPECT_TRUE(node.editor_handle->visible);
    EXPECT_FLOAT_EQ(node.editor_handle->size, 0.0f);

    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << "error: " << result.error_detail;

    auto& inst = result.instance;
    auto node_h = inst.authored_to_runtime["interactive_camera"];

    ASSERT_EQ(inst.input_receivers.size(), 1u);
    EXPECT_EQ(inst.input_receivers[0].node, node_h);
    ASSERT_EQ(inst.flying_camera_controllers.size(), 1u);
    EXPECT_EQ(inst.flying_camera_controllers[0].node, node_h);
    ASSERT_EQ(inst.audio_listeners.size(), 1u);
    EXPECT_EQ(inst.audio_listeners[0].node, node_h);
    ASSERT_EQ(inst.event_listeners.size(), 1u);
    EXPECT_EQ(inst.event_listeners[0].node, node_h);

    ASSERT_EQ(inst.editor_handles.size(), 1u);
    EXPECT_EQ(inst.editor_handles[0].node, node_h);
    EXPECT_EQ(inst.editor_handles[0].component.kind,
        SceneEditorHandleKind::Transform);
    EXPECT_TRUE(inst.editor_handles[0].component.enabled);
    EXPECT_TRUE(inst.editor_handles[0].component.visible);
    EXPECT_FLOAT_EQ(inst.editor_handles[0].component.size, 0.0f);
}

TEST(SceneAssetModule, RenderableNodeWithEditorHandle)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_renderable_editor_handle_test");

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

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    SceneAssetData scene{};
    scene.name = "renderable_editor_handle_scene";

    SceneNodeAsset node{};
    node.id = "rock_front_left";
    node.local.translation[0] = -4.0f;
    node.local.translation[2] = 8.0f;
    node.renderable_asset = renderable.output;
    node.editor_handle = SceneEditorHandleAsset{
        .kind = SceneEditorHandleKind::Transform,
        .enabled = false,
        .visible = true,
        .size = 1.25f,
    };
    scene.nodes.push_back(std::move(node));

    TestRenderableResolver resolver(assets.renderables());
    SceneInstantiateContext context{ .renderable_resolver = &resolver };

    auto result = instantiate_scene(scene, context);
    ASSERT_TRUE(result.ok()) << "error: " << result.error_detail;

    auto& inst = result.instance;
    ASSERT_EQ(wz::core::graph::node_count(inst.storage.polytree), 1u);
    ASSERT_TRUE(inst.authored_to_runtime.contains("rock_front_left"));
    auto node_h = inst.authored_to_runtime["rock_front_left"];

    EXPECT_EQ(inst.renderables[node_h].node_class.role,
        wz::scene::SceneRole::Renderable);

    ASSERT_EQ(inst.editor_handles.size(), 1u);
    EXPECT_EQ(inst.editor_handles[0].node, node_h);
    EXPECT_EQ(inst.editor_handles[0].component.kind,
        SceneEditorHandleKind::Transform);
    EXPECT_FALSE(inst.editor_handles[0].component.enabled);
    EXPECT_TRUE(inst.editor_handles[0].component.visible);
    EXPECT_FLOAT_EQ(inst.editor_handles[0].component.size, 1.25f);
}

