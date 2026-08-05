#include "scene_asset_module_test_support.h"

TEST(SceneAssetModule, InstantiatesSceneAndProducesOneDrawCommand)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_asset_draw_test");

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
    ASSERT_TRUE(report.ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);

    // Instantiate scene
    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << "error: " << result.error_detail;

    auto& inst = result.instance;

    // Validate structure
    uint32_t nc = wz::core::graph::node_count(inst.storage.polytree);
    EXPECT_EQ(nc, 2u);
    EXPECT_EQ(inst.renderables.size(), nc);
    EXPECT_EQ(inst.runtime_to_authored.size(), nc);
    EXPECT_EQ(inst.authored_to_runtime.size(), 2u);
    EXPECT_TRUE(inst.authored_to_runtime.contains("root"));
    EXPECT_TRUE(inst.authored_to_runtime.contains("debug_object"));

    // The object should have world z = 3.0 (from translation [2, 0, 3])
    auto object_h = inst.authored_to_runtime["debug_object"];
    const auto& object_world = wz::core::graph::node_data(
        inst.storage.polytree, object_h).world;
    EXPECT_FLOAT_EQ(object_world.m[12], 2.0f);
    EXPECT_FLOAT_EQ(object_world.m[14], 3.0f);

    // Object should have a renderable descriptor
    EXPECT_EQ(inst.renderables[object_h].mesh, 0u);
    EXPECT_EQ(inst.renderables[object_h].material, 0u);

    // Root should have empty/default descriptor
    auto root_h = inst.authored_to_runtime["root"];
    EXPECT_EQ(inst.renderables[root_h].node_class.role, wz::scene::SceneRole::None);

    // Now run through the existing render pipeline
    wz::scene::ViewData view{};
    view.camera_position = { 0.f, 0.f, 0.f };
    view.view = wz::math::Mat4::identity();

    constexpr float Pi = 3.14159265358979323846f;
    const float fov = 70.0f * Pi / 180.0f;
    view.projection = wz::math::projection_perspective_dx(fov, 16.f / 9.f, 0.1f, 100.f);
    view.view_projection = wz::math::mul(view.projection, view.view);

    wz::scene::CompiledSceneStorage compiled{};
    wz::scene::compile(
        compiled,
        inst.storage.polytree,
        inst.renderables,
        inst.lights,
        view);

    EXPECT_EQ(compiled.scene.opaque.size(), 1u);
    if (!compiled.scene.opaque.empty()) {
        EXPECT_EQ(compiled.scene.opaque[0].mesh, 0u);
        EXPECT_EQ(compiled.scene.opaque[0].material, 0u);
        EXPECT_FLOAT_EQ(compiled.scene.opaque[0].world.m[12], 2.0f);
        EXPECT_FLOAT_EQ(compiled.scene.opaque[0].world.m[14], 3.0f);
    }

    wz::render::RenderIRStorage render_ir{};
    wz::render::build_render_ir(render_ir, compiled.scene);

    wz::render::RenderFrameStorage render_frame{};
    wz::render::build_frame(render_frame, render_ir.ir, compiled.scene);

    ASSERT_EQ(render_frame.frame.opaque.size(), 1u);

    const auto& cmd = render_frame.frame.opaque[0];
    EXPECT_EQ(cmd.stage, wz::render::PipelineStage::OpaqueGeometry);
    EXPECT_EQ(cmd.mesh, 0u);
    EXPECT_EQ(cmd.material, 0u);
    EXPECT_FLOAT_EQ(cmd.world.m[12], 2.0f);
    EXPECT_FLOAT_EQ(cmd.world.m[14], 3.0f);
}

TEST(SceneAssetModule, ParentChildProducesExpectedWorldTransform)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_asset_parent_child_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    auto rel_path = write_scene_json(root, "parent_child.json", kParentChildSceneJSON);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "parent_child",
            .path = rel_path,
            });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);

    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok());

    auto& inst = result.instance;

    // Child world = parent_world * child_local
    // parent at (2, 0, 5), child local at (0, 3, 0)
    // expected child world: (2, 3, 5)
    auto child_h = inst.authored_to_runtime["child"];
    const auto& child_world = wz::core::graph::node_data(
        inst.storage.polytree, child_h).world;
    EXPECT_FLOAT_EQ(child_world.m[12], 2.0f);
    EXPECT_FLOAT_EQ(child_world.m[13], 3.0f);
    EXPECT_FLOAT_EQ(child_world.m[14], 5.0f);
}

TEST(SceneAssetModule, ParentChildProducesDrawCommandWithInheritedTransform)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_asset_parent_child_draw_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    auto rel_path = write_scene_json(root, "parent_child.json", kParentChildSceneJSON);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "parent_child",
            .path = rel_path,
            });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);

    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok());

    auto& inst = result.instance;

    wz::scene::ViewData view{};
    view.camera_position = { 0.f, 0.f, 0.f };
    view.view = wz::math::Mat4::identity();

    constexpr float Pi = 3.14159265358979323846f;
    const float fov = 70.0f * Pi / 180.0f;
    view.projection = wz::math::projection_perspective_dx(fov, 16.f / 9.f, 0.1f, 100.f);
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
    EXPECT_FLOAT_EQ(compiled.scene.opaque[0].world.m[13], 3.0f);
    EXPECT_FLOAT_EQ(compiled.scene.opaque[0].world.m[14], 5.0f);
    EXPECT_EQ(compiled.scene.opaque[0].mesh, 1u);
    EXPECT_EQ(compiled.scene.opaque[0].material, 1u);

    wz::render::RenderIRStorage render_ir{};
    wz::render::build_render_ir(render_ir, compiled.scene);

    wz::render::RenderFrameStorage render_frame{};
    wz::render::build_frame(render_frame, render_ir.ir, compiled.scene);

    ASSERT_EQ(render_frame.frame.opaque.size(), 1u);

    const auto& cmd = render_frame.frame.opaque[0];
    EXPECT_EQ(cmd.stage, wz::render::PipelineStage::OpaqueGeometry);
    EXPECT_EQ(cmd.mesh, 1u);
    EXPECT_EQ(cmd.material, 1u);
    EXPECT_FLOAT_EQ(cmd.world.m[12], 2.0f);
    EXPECT_FLOAT_EQ(cmd.world.m[13], 3.0f);
    EXPECT_FLOAT_EQ(cmd.world.m[14], 5.0f);
}

TEST(SceneAssetModule, NonRenderableNodeProducesNoRenderOutput)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_asset_non_renderable_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    auto rel_path = write_scene_json(root, "mixed.json", kNonRenderableNodeSceneJSON);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "mixed_scene",
            .path = rel_path,
            });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);

    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok());

    auto& inst = result.instance;

    EXPECT_EQ(wz::core::graph::node_count(inst.storage.polytree), 3u);
    EXPECT_EQ(inst.renderables.size(), 3u);

    // empty_node should have no renderable
    auto empty_h = inst.authored_to_runtime["empty_node"];
    EXPECT_EQ(inst.renderables[empty_h].node_class.role, wz::scene::SceneRole::None);

    // Compile and verify only one opaque primitive
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

TEST(SceneInstantiate, RejectsDuplicateNodeIds)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "dup_test";
    scene.nodes.push_back({ .id = "a" });
    scene.nodes.push_back({ .id = "a" });

    auto result = instantiate_scene(scene);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, SceneInstantiateError::DuplicateNodeId);
}

TEST(SceneInstantiate, RejectsInvalidParentId)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "bad_parent";
    scene.nodes.push_back({ .id = "a", .parent_id = "nonexistent" });

    auto result = instantiate_scene(scene);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, SceneInstantiateError::ParentNotFound);
}

TEST(SceneInstantiate, RejectsParentCycle)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "cycle";
    scene.nodes.push_back({ .id = "a", .parent_id = "b" });
    scene.nodes.push_back({ .id = "b", .parent_id = "a" });

    auto result = instantiate_scene(scene);
    EXPECT_FALSE(result.ok());
    // A3-H3 (#77 visit 2): a >= 2-node cycle now reports ParentCycle with a node
    // named in error_detail, like the self-parent case, instead of an empty
    // PolytreeBuildFailed.
    EXPECT_EQ(result.error, SceneInstantiateError::ParentCycle);
    EXPECT_FALSE(result.error_detail.empty());
}

TEST(SceneAssetModule, CameraNodePopulatesDefaultView)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_asset_camera_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    auto rel_path = write_scene_json(root, "camera_scene.json", kCameraSceneJSON);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "camera_scene",
            .path = rel_path,
            });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);

    // Verify the camera data was parsed
    ASSERT_EQ(scene_data->defaults.active_camera_node, "main_camera");
    bool found_camera_node = false;
    for (const auto& node : scene_data->nodes) {
        if (node.id == "main_camera") {
            ASSERT_TRUE(node.camera.has_value());
            EXPECT_NEAR(node.camera->fov_y, 1.0472f, 1e-4f);
            EXPECT_NEAR(node.camera->near_plane, 0.1f, 1e-4f);
            EXPECT_NEAR(node.camera->far_plane, 500.0f, 1e-2f);
            EXPECT_NEAR(node.camera->aspect, 1.7778f, 1e-3f);
            found_camera_node = true;
        }
    }
    ASSERT_TRUE(found_camera_node);

    // Instantiate and verify default_view is populated
    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok());

    auto& inst = result.instance;
    const auto& dv = inst.default_view;

    // Camera at world (0, 5, 10) — from parent (0,0,0) + local (0,5,10).
    EXPECT_FLOAT_EQ(dv.camera_position.x, 0.0f);
    EXPECT_FLOAT_EQ(dv.camera_position.y, 5.0f);
    EXPECT_FLOAT_EQ(dv.camera_position.z, 10.0f);

    // View matrix: identity rotation, so the view matrix should be a pure
    // translation by the negated camera position.
    // V = transpose(I) with t = -I * (0,5,10) = (0,-5,-10).
    EXPECT_FLOAT_EQ(dv.view.m[12], 0.0f);
    EXPECT_FLOAT_EQ(dv.view.m[13], -5.0f);
    EXPECT_FLOAT_EQ(dv.view.m[14], -10.0f);

    // Rotation part should be identity (no rotation on the camera node).
    EXPECT_FLOAT_EQ(dv.view.m[0], 1.0f);
    EXPECT_FLOAT_EQ(dv.view.m[5], 1.0f);
    EXPECT_FLOAT_EQ(dv.view.m[10], 1.0f);

    // Projection should be non-zero (a valid perspective matrix).
    EXPECT_NE(dv.projection.m[0], 0.0f);
    EXPECT_NE(dv.projection.m[5], 0.0f);

    // view_projection should equal projection * view.
    wz::math::Mat4 expected_vp = wz::math::mul(dv.projection, dv.view);
    for (int i = 0; i < 16; ++i) {
        EXPECT_NEAR(dv.view_projection.m[i], expected_vp.m[i], 1e-5f)
            << "view_projection mismatch at index " << i;
    }

    // Verify the scene can still render: the object at (0,0,5) should be
    // visible from the camera at (0,5,10) looking down -Z.
    wz::scene::CompiledSceneStorage compiled{};
    wz::scene::compile(
        compiled,
        inst.storage.polytree,
        inst.renderables,
        inst.lights,
        dv);

    EXPECT_EQ(compiled.scene.opaque.size(), 1u);
    if (!compiled.scene.opaque.empty()) {
        EXPECT_FLOAT_EQ(compiled.scene.opaque[0].world.m[12], 0.0f);
        EXPECT_FLOAT_EQ(compiled.scene.opaque[0].world.m[14], 5.0f);
    }
}

TEST(SceneAssetModule, CameraInheritsParentTransformForDefaultView)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_asset_camera_inherit_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    auto rel_path = write_scene_json(
        root, "camera_inherit.json", kCameraInheritanceSceneJSON);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "camera_inherit",
            .path = rel_path,
            });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);

    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok());

    auto& inst = result.instance;
    const auto& dv = inst.default_view;

    // Camera world position must reflect the propagated transform, not
    // just the local (0,0,5).  Parent at (10,0,0) with 90° Y rotation
    // maps child local +Z to world +X, so world pos = (10+5, 0, 0).
    EXPECT_NEAR(dv.camera_position.x, 15.0f, 1e-4f);
    EXPECT_NEAR(dv.camera_position.y, 0.0f, 1e-4f);
    EXPECT_NEAR(dv.camera_position.z, 0.0f, 1e-4f);

    // View matrix rotation should NOT be identity — the 90° Y rotation
    // from the parent must be present.  After the rotation:
    //   world column 0 (right)   = (0, 0, -1)
    //   world column 1 (up)      = (0, 1,  0)
    //   world column 2 (forward) = (1, 0,  0)
    // Transposed into the view matrix rows:
    EXPECT_NEAR(dv.view.m[0],  0.0f, 1e-4f);   // row0: right.x
    EXPECT_NEAR(dv.view.m[4],  0.0f, 1e-4f);   // row0: right.y
    EXPECT_NEAR(dv.view.m[8], -1.0f, 1e-4f);   // row0: right.z
    EXPECT_NEAR(dv.view.m[5],  1.0f, 1e-4f);   // row1: up.y
    EXPECT_NEAR(dv.view.m[2],  1.0f, 1e-4f);   // row2: forward.x
    EXPECT_NEAR(dv.view.m[6],  0.0f, 1e-4f);   // row2: forward.y
    EXPECT_NEAR(dv.view.m[10], 0.0f, 1e-4f);   // row2: forward.z

    // Translation part: V.m[14] = -(fx*px + fy*py + fz*pz)
    //                            = -(1*15 + 0*0 + 0*0) = -15
    EXPECT_NEAR(dv.view.m[12],  0.0f, 1e-4f);
    EXPECT_NEAR(dv.view.m[13],  0.0f, 1e-4f);
    EXPECT_NEAR(dv.view.m[14], -15.0f, 1e-4f);

    // Sanity: view_projection = projection * view.
    wz::math::Mat4 expected_vp = wz::math::mul(dv.projection, dv.view);
    for (int i = 0; i < 16; ++i) {
        EXPECT_NEAR(dv.view_projection.m[i], expected_vp.m[i], 1e-4f)
            << "view_projection mismatch at index " << i;
    }

    // The target object at (20,0,0) is 5 units ahead of the camera
    // along its forward direction (world +X).  It should be visible
    // and produce a draw command when rendered with this view.
    wz::scene::CompiledSceneStorage compiled{};
    wz::scene::compile(
        compiled,
        inst.storage.polytree,
        inst.renderables,
        inst.lights,
        dv);

    EXPECT_EQ(compiled.scene.opaque.size(), 1u);
    if (!compiled.scene.opaque.empty()) {
        EXPECT_FLOAT_EQ(compiled.scene.opaque[0].world.m[12], 20.0f);
    }
}

