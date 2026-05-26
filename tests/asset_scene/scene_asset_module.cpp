// tests/asset_scene/scene_asset_module.cpp

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/scene/scene_instance.h>
#include <engine/assets/scene/scene_json_export.h>
#include <engine/rendering/scene_render_resource_resolver.h>
#include <engine/rendering/render_resource_resolver.h>

#include <external/json/json_writer.h>

#include <scene/compile/scene_compiler.h>
#include <render/ir/render_ir.h>
#include <render/frame/render_frame.h>

#include <math/mat4.h>
#include <math/projection.h>
#include <math/quaternion.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

namespace
{
    const char* kSingleNodeSceneJSON = R"({
  "schema": "wozzits.scene.v0",
  "name": "single_object_scene",
  "nodes": [
    {
      "id": "root",
      "name": "root",
      "transform": {
        "translation": [0, 0, 0]
      }
    },
    {
      "id": "debug_object",
      "parent": "root",
      "transform": {
        "translation": [2, 0, 3]
      },
      "debug_renderable": {
        "pipeline": "OpaqueGeometry",
        "mesh": 0,
        "material": 0,
        "bounds": {
          "min": [-0.5, -0.5, -0.5],
          "max": [0.5, 0.5, 0.5]
        }
      }
    }
  ]
})";

    const char* kParentChildSceneJSON = R"({
  "schema": "wozzits.scene.v0",
  "name": "parent_child_scene",
  "nodes": [
    {
      "id": "parent",
      "transform": {
        "translation": [2, 0, 5]
      }
    },
    {
      "id": "child",
      "parent": "parent",
      "transform": {
        "translation": [0, 3, 0]
      },
      "debug_renderable": {
        "pipeline": "OpaqueGeometry",
        "mesh": 1,
        "material": 1,
        "bounds": {
          "min": [-0.5, -0.5, -0.5],
          "max": [0.5, 0.5, 0.5]
        }
      }
    }
  ]
})";

    const char* kCameraSceneJSON = R"({
  "schema": "wozzits.scene.v0",
  "name": "camera_scene",
  "nodes": [
    {
      "id": "world_root",
      "transform": {
        "translation": [0, 0, 0]
      }
    },
    {
      "id": "main_camera",
      "parent": "world_root",
      "transform": {
        "translation": [0, 5, 10]
      },
      "camera": {
        "fov_y": 1.0472,
        "near": 0.1,
        "far": 500.0,
        "aspect": 1.7778
      }
    },
    {
      "id": "object",
      "parent": "world_root",
      "transform": {
        "translation": [0, 0, 5]
      },
      "debug_renderable": {
        "pipeline": "OpaqueGeometry",
        "mesh": 0,
        "material": 0,
        "bounds": {
          "min": [-1, -1, -1],
          "max": [1, 1, 1]
        }
      }
    }
  ],
  "defaults": {
    "active_camera": "main_camera"
  }
})";

    // Camera under a parent with both translation and 90° Y rotation.
    // Parent at (10,0,0) rotated 90° around Y (quat: 0, 0.7071, 0, 0.7071).
    // Camera child has local translation (0,0,5).
    // Under 90° Y rotation, local +Z maps to world +X,
    // so expected camera world position = (10+5, 0, 0) = (15, 0, 0).
    // Object at (20,0,0) is 5 units ahead of camera along its forward (+X world).
    const char* kCameraInheritanceSceneJSON = R"({
  "schema": "wozzits.scene.v0",
  "name": "camera_inheritance_scene",
  "nodes": [
    {
      "id": "rotated_parent",
      "transform": {
        "translation": [10, 0, 0],
        "rotation_quat": [0, 0.70710678, 0, 0.70710678]
      }
    },
    {
      "id": "child_camera",
      "parent": "rotated_parent",
      "transform": {
        "translation": [0, 0, 5]
      },
      "camera": {
        "fov_y": 1.0472,
        "near": 0.1,
        "far": 200.0,
        "aspect": 1.7778
      }
    },
    {
      "id": "target_object",
      "transform": {
        "translation": [20, 0, 0]
      },
      "debug_renderable": {
        "pipeline": "OpaqueGeometry",
        "mesh": 0,
        "material": 0,
        "bounds": {
          "min": [-1, -1, -1],
          "max": [1, 1, 1]
        }
      }
    }
  ],
  "defaults": {
    "active_camera": "child_camera"
  }
})";

    const char* kFlyCameraDescriptorSceneJSON = R"({
  "schema": "wozzits.scene.v0",
  "name": "fly_camera_descriptor_test",
  "nodes": [
    {
      "id": "editor_fly_camera",
      "transform": {
        "translation": [0, 5, -20]
      },
      "camera": {
        "fov_y": 1.0472,
        "near": 0.1,
        "far": 5000.0,
        "aspect": 1.7778
      },
      "input_receiver": {
        "input_map": "asset://input_maps/editor_fly_camera"
      },
      "flying_camera_controller": {
        "move_speed": 20.0,
        "look_speed": 0.0005,
        "boost_multiplier": 3.0,
        "roll_speed": 1.5
      }
    }
  ],
  "defaults": {
    "active_camera": "editor_fly_camera"
  }
})";

    const char* kListenerOnlySceneJSON = R"({
  "schema": "wozzits.scene.v0",
  "name": "listener_descriptor_test",
  "nodes": [
    {
      "id": "listener",
      "transform": {
        "translation": [0, 0, 0]
      },
      "audio_listener": {
        "active": true
      },
      "event_listener": {
        "channels": ["gameplay", "ui"]
      }
    }
  ]
})";

    // ─── Issue #57: debug visual descriptor test scenes ───────────────────

    const char* kDebugAxesDescriptorSceneJSON = R"({
  "schema": "wozzits.scene.v0",
  "name": "debug_axes_descriptor_test",
  "nodes": [
    {
      "id": "empty_anchor",
      "parent": null,
      "transform": {
        "translation": [2, 3, 4]
      },
      "debug_visual": {
        "kind": "axes",
        "scale": 1.5,
        "visible": true
      }
    }
  ]
})";

    const char* kCameraWithDebugVisualSceneJSON = R"({
  "schema": "wozzits.scene.v0",
  "name": "camera_debug_visual_test",
  "nodes": [
    {
      "id": "cam_node",
      "transform": {
        "translation": [0, 5, -10]
      },
      "camera": {
        "fov_y": 1.0472,
        "near": 0.1,
        "far": 500.0,
        "aspect": 1.7778
      },
      "debug_visual": {
        "kind": "axes",
        "scale": 0.5,
        "visible": true
      }
    }
  ],
  "defaults": {
    "active_camera": "cam_node"
  }
})";

    const char* kFlyCameraWithDebugVisualSceneJSON = R"({
  "schema": "wozzits.scene.v0",
  "name": "fly_camera_debug_visual_test",
  "nodes": [
    {
      "id": "editor_fly_cam",
      "transform": {
        "translation": [0, 10, -30]
      },
      "camera": {
        "fov_y": 1.0472,
        "near": 0.1,
        "far": 5000.0,
        "aspect": 1.7778
      },
      "input_receiver": {
        "input_map": "asset://input_maps/editor_fly"
      },
      "flying_camera_controller": {
        "move_speed": 15.0,
        "look_speed": 0.001,
        "boost_multiplier": 5.0,
        "roll_speed": 2.0
      },
      "debug_visual": {
        "kind": "axes",
        "scale": 2.0,
        "visible": false
      }
    }
  ],
  "defaults": {
    "active_camera": "editor_fly_cam"
  }
})";

    const char* kDebugVisualDefaultVisibleSceneJSON = R"({
  "schema": "wozzits.scene.v0",
  "name": "debug_visual_default_visible",
  "nodes": [
    {
      "id": "anchor",
      "debug_visual": {
        "kind": "axes",
        "scale": 1.0
      }
    }
  ]
})";

    const char* kEditorHandleAnchorSceneJSON = R"({
  "schema": "wozzits.scene.v0",
  "name": "editor_handle_anchor_test",
  "nodes": [
    {
      "id": "terrain_anchor",
      "parent": null,
      "transform": {
        "translation": [3, 0, 7]
      },
      "debug_visual": {
        "kind": "axes",
        "scale": 1.0,
        "visible": true
      },
      "editor_handle": {
        "kind": "translate",
        "enabled": true,
        "visible": false,
        "size": 2.5
      }
    }
  ]
})";

    const char* kEditorHandleMixedDescriptorsSceneJSON = R"({
  "schema": "wozzits.scene.v0",
  "name": "editor_handle_mixed_descriptors_test",
  "nodes": [
    {
      "id": "interactive_camera",
      "transform": {
        "translation": [0, 3, -8]
      },
      "camera": {
        "fov_y": 1.0472,
        "near": 0.1,
        "far": 500.0,
        "aspect": 1.7778
      },
      "input_receiver": {
        "input_map": "asset://input_maps/editor"
      },
      "flying_camera_controller": {
        "move_speed": 10.0
      },
      "audio_listener": {
        "active": true
      },
      "event_listener": {
        "channels": ["editor"]
      },
      "editor_handle": {
        "kind": "transform",
        "size": 0.0
      }
    }
  ],
  "defaults": {
    "active_camera": "interactive_camera"
  }
})";

    const char* kNonRenderableNodeSceneJSON = R"({
  "schema": "wozzits.scene.v0",
  "name": "mixed_scene",
  "nodes": [
    {
      "id": "root",
      "transform": {
        "translation": [0, 0, 0]
      }
    },
    {
      "id": "empty_node",
      "parent": "root",
      "transform": {
        "translation": [1, 0, 0]
      }
    },
    {
      "id": "visible_object",
      "parent": "root",
      "transform": {
        "translation": [0, 0, 5]
      },
      "debug_renderable": {
        "pipeline": "OpaqueGeometry",
        "mesh": 0,
        "material": 0
      }
    }
  ]
})";

    wz::fs::Path write_scene_json(
        const wz::fs::Path& root,
        const std::string& filename,
        const std::string& content)
    {
        wz::fs::Path path = wz::fs::join(root, filename);
        wz::fs::write_file_text(path, content);
        return filename;
    }
}

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
    EXPECT_EQ(result.error, SceneInstantiateError::PolytreeBuildFailed);
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

// ─── Issue #56: non-render component descriptors ────────────────────────

TEST(SceneAssetModule, FlyCameraComponentDescriptors)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_fly_camera_desc_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    auto rel_path = write_scene_json(
        root, "fly_camera_desc.json", kFlyCameraDescriptorSceneJSON);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "fly_camera_desc",
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
    EXPECT_EQ(node.id, "editor_fly_camera");
    ASSERT_TRUE(node.camera.has_value());
    ASSERT_TRUE(node.input_receiver.has_value());
    ASSERT_TRUE(node.flying_camera_controller.has_value());
    EXPECT_EQ(node.input_receiver->input_map,
        "asset://input_maps/editor_fly_camera");
    EXPECT_FLOAT_EQ(node.flying_camera_controller->move_speed, 20.0f);
    EXPECT_FLOAT_EQ(node.flying_camera_controller->look_speed, 0.0005f);
    EXPECT_FLOAT_EQ(node.flying_camera_controller->boost_multiplier, 3.0f);
    EXPECT_FLOAT_EQ(node.flying_camera_controller->roll_speed, 1.5f);

    // Node should NOT have audio/event listeners
    EXPECT_FALSE(node.audio_listener.has_value());
    EXPECT_FALSE(node.event_listener.has_value());

    // Instantiate
    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok());

    auto& inst = result.instance;

    // One node in the graph
    EXPECT_EQ(wz::core::graph::node_count(inst.storage.polytree), 1u);
    EXPECT_TRUE(inst.authored_to_runtime.contains("editor_fly_camera"));
    auto cam_h = inst.authored_to_runtime["editor_fly_camera"];

    // Active camera default view should still work
    EXPECT_NEAR(inst.default_view.camera_position.y, 5.0f, 1e-4f);
    EXPECT_NEAR(inst.default_view.camera_position.z, -20.0f, 1e-4f);
    EXPECT_NE(inst.default_view.projection.m[0], 0.0f);

    // Component records: one input receiver, one flying camera controller
    ASSERT_EQ(inst.input_receivers.size(), 1u);
    EXPECT_EQ(inst.input_receivers[0].node, cam_h);
    EXPECT_EQ(inst.input_receivers[0].component.input_map,
        "asset://input_maps/editor_fly_camera");

    ASSERT_EQ(inst.flying_camera_controllers.size(), 1u);
    EXPECT_EQ(inst.flying_camera_controllers[0].node, cam_h);
    EXPECT_FLOAT_EQ(
        inst.flying_camera_controllers[0].component.move_speed, 20.0f);
    EXPECT_FLOAT_EQ(
        inst.flying_camera_controllers[0].component.look_speed, 0.0005f);
    EXPECT_FLOAT_EQ(
        inst.flying_camera_controllers[0].component.boost_multiplier, 3.0f);
    EXPECT_FLOAT_EQ(
        inst.flying_camera_controllers[0].component.roll_speed, 1.5f);

    // No audio/event component records
    EXPECT_TRUE(inst.audio_listeners.empty());
    EXPECT_TRUE(inst.event_listeners.empty());

    // No renderable — node is camera+controller only
    EXPECT_EQ(inst.renderables[cam_h].node_class.role,
        wz::scene::SceneRole::None);
}

TEST(SceneAssetModule, ListenerOnlyNodeDescriptors)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_listener_desc_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    auto rel_path = write_scene_json(
        root, "listener_desc.json", kListenerOnlySceneJSON);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "listener_desc",
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
    EXPECT_EQ(node.id, "listener");
    EXPECT_FALSE(node.renderable.has_value());
    EXPECT_FALSE(node.camera.has_value());
    ASSERT_TRUE(node.audio_listener.has_value());
    EXPECT_TRUE(node.audio_listener->active);
    ASSERT_TRUE(node.event_listener.has_value());
    ASSERT_EQ(node.event_listener->channels.size(), 2u);
    EXPECT_EQ(node.event_listener->channels[0], "gameplay");
    EXPECT_EQ(node.event_listener->channels[1], "ui");

    // No input/controller
    EXPECT_FALSE(node.input_receiver.has_value());
    EXPECT_FALSE(node.flying_camera_controller.has_value());

    // Instantiate
    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok());

    auto& inst = result.instance;

    EXPECT_EQ(wz::core::graph::node_count(inst.storage.polytree), 1u);
    EXPECT_TRUE(inst.authored_to_runtime.contains("listener"));
    auto listen_h = inst.authored_to_runtime["listener"];

    // No render output
    EXPECT_EQ(inst.renderables[listen_h].node_class.role,
        wz::scene::SceneRole::None);

    // Audio listener record
    ASSERT_EQ(inst.audio_listeners.size(), 1u);
    EXPECT_EQ(inst.audio_listeners[0].node, listen_h);
    EXPECT_TRUE(inst.audio_listeners[0].component.active);

    // Event listener record
    ASSERT_EQ(inst.event_listeners.size(), 1u);
    EXPECT_EQ(inst.event_listeners[0].node, listen_h);
    ASSERT_EQ(inst.event_listeners[0].component.channels.size(), 2u);
    EXPECT_EQ(inst.event_listeners[0].component.channels[0], "gameplay");
    EXPECT_EQ(inst.event_listeners[0].component.channels[1], "ui");

    // No input/controller records
    EXPECT_TRUE(inst.input_receivers.empty());
    EXPECT_TRUE(inst.flying_camera_controllers.empty());

    // Compile with identity view — should produce zero render output
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

// ─── Descriptor validation (negative) tests ─────────────────────────────

namespace
{
    // Helper: write JSON, push through the pipeline, return whether
    // compiled scene data was produced.  A validation rejection in
    // parse_node → compile_failed_node → get_scene_data returns nullptr.
    bool scene_json_compiles(
        const std::string& dir_suffix,
        const std::string& json)
    {
        wz::fs::Path root = wz::fs::join(
            wz::fs::temp_directory_path(),
            "wz_scene_val_" + dir_suffix);
        wz::fs::create_directories(root);

        wz::Logger logger;
        wz::gpu::Device device{};
        wz::engine::assets::EngineAssetLibrary assets{
            device, logger, root };

        using namespace wz::engine::assets;

        auto rel = write_scene_json(root, "test.json", json);
        auto sa = assets.scenes().create_scene_from_json({
            .name = "val_test", .path = rel });
        if (!sa.valid()) return false;
        if (!assets.commit()) return false;
        assets.resolve_all();

        const auto* data = assets.scenes().get_scene_data(
            assets.scenes().get_scene(sa));
        return data != nullptr;
    }
}

TEST(SceneDescriptorValidation, RejectsMissingInputMap)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "bad_input_receiver",
  "nodes": [{
    "id": "n",
    "input_receiver": {}
  }]
})";
    EXPECT_FALSE(scene_json_compiles("missing_input_map", json));
}

TEST(SceneDescriptorValidation, RejectsEmptyInputMap)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "empty_input_map",
  "nodes": [{
    "id": "n",
    "input_receiver": { "input_map": "" }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("empty_input_map", json));
}

TEST(SceneDescriptorValidation, RejectsNegativeMoveSpeed)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "neg_move_speed",
  "nodes": [{
    "id": "n",
    "flying_camera_controller": { "move_speed": -1.0 }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("neg_move_speed", json));
}

TEST(SceneDescriptorValidation, RejectsNegativeLookSpeed)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "neg_look_speed",
  "nodes": [{
    "id": "n",
    "flying_camera_controller": { "look_speed": -0.001 }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("neg_look_speed", json));
}

TEST(SceneDescriptorValidation, RejectsNegativeBoostMultiplier)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "neg_boost",
  "nodes": [{
    "id": "n",
    "flying_camera_controller": { "boost_multiplier": -2.0 }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("neg_boost", json));
}

TEST(SceneDescriptorValidation, RejectsNegativeRollSpeed)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "neg_roll",
  "nodes": [{
    "id": "n",
    "flying_camera_controller": { "roll_speed": -0.5 }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("neg_roll", json));
}

TEST(SceneDescriptorValidation, RejectsEmptyEventChannels)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "empty_channels",
  "nodes": [{
    "id": "n",
    "event_listener": { "channels": [] }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("empty_channels", json));
}

TEST(SceneDescriptorValidation, RejectsAllBlankEventChannels)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "blank_channels",
  "nodes": [{
    "id": "n",
    "event_listener": { "channels": ["", ""] }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("blank_channels", json));
}

TEST(SceneDescriptorValidation, AcceptsZeroSpeedValues)
{
    // Zero is a valid edge case — means "no movement" until overridden.
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "zero_speeds",
  "nodes": [{
    "id": "n",
    "flying_camera_controller": {
      "move_speed": 0.0,
      "look_speed": 0.0,
      "boost_multiplier": 0.0,
      "roll_speed": 0.0
    }
  }]
})";
    EXPECT_TRUE(scene_json_compiles("zero_speeds", json));
}

// ─── Issue #57: debug visual descriptor tests ───────────────────────────

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
    ASSERT_EQ(inst.debug_visuals.size(), 1u);
    EXPECT_EQ(inst.debug_visuals[0].node, anchor_h);
    EXPECT_EQ(inst.debug_visuals[0].component.kind, SceneDebugVisualKind::Axes);
    EXPECT_FLOAT_EQ(inst.debug_visuals[0].component.scale, 1.5f);
    EXPECT_TRUE(inst.debug_visuals[0].component.visible);

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
    ASSERT_EQ(inst.debug_visuals.size(), 1u);
    EXPECT_EQ(inst.debug_visuals[0].node, cam_h);
    EXPECT_EQ(inst.debug_visuals[0].component.kind, SceneDebugVisualKind::Axes);
    EXPECT_FLOAT_EQ(inst.debug_visuals[0].component.scale, 0.5f);

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

    ASSERT_EQ(inst.debug_visuals.size(), 1u);
    EXPECT_EQ(inst.debug_visuals[0].node, fly_h);
    EXPECT_EQ(inst.debug_visuals[0].component.kind, SceneDebugVisualKind::Axes);
    EXPECT_FLOAT_EQ(inst.debug_visuals[0].component.scale, 2.0f);
    EXPECT_FALSE(inst.debug_visuals[0].component.visible);

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

    ASSERT_EQ(result.instance.debug_visuals.size(), 1u);
    EXPECT_TRUE(result.instance.debug_visuals[0].component.visible);
}

// ─── Issue #57: debug visual validation tests ───────────────────────────

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

// Issue #61: editor handle validation tests. Missing kind is rejected so
// authored tool intent stays explicit.

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

// ─── Issue #58: renderable asset reference tests ────────────────────────

namespace
{
    class TestRenderableResolver final
        : public wz::engine::assets::SceneRenderableResolver
    {
    public:
        TestRenderableResolver(
            wz::engine::assets::RenderableAssetModule& mod)
            : module_(mod) {}

        const wz::engine::assets::RenderableAssetData* get(
            wz::asset::AssetKey key) const override
        {
            wz::engine::assets::RenderableAsset asset{ .output = key };
            auto handle = module_.get_renderable(asset);
            if (!handle.valid()) return nullptr;
            return module_.get_renderable_data(handle);
        }

    private:
        wz::engine::assets::RenderableAssetModule& module_;
    };
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

    ASSERT_EQ(inst.debug_visuals.size(), 1u);
    EXPECT_EQ(inst.debug_visuals[0].node, anchor_h);

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

    const auto renderable = assets.renderables().create_mesh_wireframe({
        .name = "debug/cube_wireframe",
        .mesh = mesh,
    });
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

TEST(SceneAssetModule, MeshWireframeRenderableInScene)
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

    const auto renderable = assets.renderables().create_mesh_wireframe({
        .name = "debug/cube_wireframe",
        .mesh = mesh,
    });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    // Verify the renderable resolved
    const auto rhandle = assets.renderables().get_renderable(renderable);
    ASSERT_TRUE(rhandle.valid());
    const auto* rdata = assets.renderables().get_renderable_data(rhandle);
    ASSERT_NE(rdata, nullptr);
    EXPECT_EQ(rdata->kind, RenderableKind::Mesh);

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
    SceneInstantiateContext context{ .renderable_resolver = &resolver };

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

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "debug/cube",
        .kind = ProceduralMeshKind::Cube,
    });
    const auto renderable = assets.renderables().create_mesh_wireframe({
        .name = "debug/cube_wireframe",
        .mesh = mesh,
    });

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
    SceneInstantiateContext context{ .renderable_resolver = &resolver };

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
    ASSERT_EQ(inst.debug_visuals.size(), 1u);
    EXPECT_EQ(inst.debug_visuals[0].node, cam_h);

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

TEST(SceneInstantiate, RejectsRenderableAssetWithoutResolver)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "no_resolver";

    SceneNodeAsset node{};
    node.id = "obj";
    node.renderable_asset = wz::asset::AssetKey{};  // any key
    scene.nodes.push_back(std::move(node));

    auto result = instantiate_scene(scene);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, SceneInstantiateError::RenderableResolveFailed);
}

TEST(SceneInstantiate, RejectsUnresolvableRenderableAsset)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_bad_renderable_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    EngineAssetLibrary assets{ device, logger, root };

    ASSERT_TRUE(assets.commit());
    assets.resolve_all();

    SceneAssetData scene{};
    scene.name = "bad_ref";

    SceneNodeAsset node{};
    node.id = "missing";
    // Fabricate a key that doesn't exist in the renderable table
    wz::asset::AssetKey fake_key{};
    fake_key.content_hash = { 0xDEADBEEF, 0 };
    node.renderable_asset = fake_key;
    scene.nodes.push_back(std::move(node));

    TestRenderableResolver resolver(assets.renderables());
    SceneInstantiateContext context{ .renderable_resolver = &resolver };

    auto result = instantiate_scene(scene, context);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, SceneInstantiateError::RenderableResolveFailed);
}

// ─── Issue #59: render resource resolver tests ────────────────────────

namespace
{
    class TestRenderResourceResolver final
        : public wz::engine::assets::SceneRenderResourceResolver
    {
    public:
        TestRenderResourceResolver(
            wz::scene::MeshHandle mesh,
            wz::scene::MaterialHandle material)
            : mesh_(mesh), material_(material) {}

        bool realize_renderable_descriptor(
            const wz::engine::assets::RenderableAssetData& renderable,
            wz::scene::RenderableDescriptor& descriptor) const override
        {
            if (renderable.kind != wz::engine::assets::RenderableKind::Mesh)
                return false;

            descriptor.mesh = mesh_;
            descriptor.material = material_;
            return true;
        }

    private:
        wz::scene::MeshHandle mesh_;
        wz::scene::MaterialHandle material_;
    };
}

TEST(SceneAssetModule, RealizedMeshHandlesFlowToDrawCommand)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_realize_mesh_test");

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
    scene.name = "realize_mesh_scene";

    SceneNodeAsset node{};
    node.id = "cube_node";
    node.local.translation[0] = 2.0f;
    node.local.translation[2] = 3.0f;
    node.renderable_asset = renderable.output;
    scene.nodes.push_back(std::move(node));

    constexpr wz::scene::MeshHandle expected_mesh = 7;
    constexpr wz::scene::MaterialHandle expected_material = 3;

    TestRenderableResolver renderable_resolver(assets.renderables());
    TestRenderResourceResolver resource_resolver(expected_mesh, expected_material);
    SceneInstantiateContext context{
        .renderable_resolver = &renderable_resolver,
        .resource_resolver = &resource_resolver,
    };

    auto result = instantiate_scene(scene, context);
    ASSERT_TRUE(result.ok()) << "error: " << result.error_detail;

    auto& inst = result.instance;
    auto cube_h = inst.authored_to_runtime["cube_node"];

    const auto& desc = inst.renderables[cube_h];
    EXPECT_EQ(desc.mesh, expected_mesh);
    EXPECT_EQ(desc.material, expected_material);
    EXPECT_EQ(desc.node_class.role, wz::scene::SceneRole::Renderable);
    EXPECT_EQ(desc.node_class.producer, wz::scene::ProducerKind::Mesh);

    // Full pipeline: compile → IR → frame
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
    EXPECT_EQ(compiled.scene.opaque[0].mesh, expected_mesh);
    EXPECT_EQ(compiled.scene.opaque[0].material, expected_material);

    wz::render::RenderIRStorage render_ir{};
    wz::render::build_render_ir(render_ir, compiled.scene);

    wz::render::RenderFrameStorage render_frame{};
    wz::render::build_frame(render_frame, render_ir.ir, compiled.scene);

    ASSERT_EQ(render_frame.frame.opaque.size(), 1u);

    const auto& cmd = render_frame.frame.opaque[0];
    EXPECT_EQ(cmd.mesh, expected_mesh);
    EXPECT_EQ(cmd.material, expected_material);
    EXPECT_FLOAT_EQ(cmd.world.m[12], 2.0f);
    EXPECT_FLOAT_EQ(cmd.world.m[14], 3.0f);
}

TEST(SceneAssetModule, RealizedHandlesWithMixedNodes)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_realize_mixed_test");

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
    const auto renderable = assets.renderables().create_mesh_wireframe({
        .name = "debug/cube_wireframe",
        .mesh = mesh,
    });

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    SceneAssetData scene{};
    scene.name = "mixed_realize_scene";

    SceneNodeAsset render_node{};
    render_node.id = "cube";
    render_node.local.translation[0] = 5.0f;
    render_node.renderable_asset = renderable.output;
    scene.nodes.push_back(std::move(render_node));

    SceneNodeAsset camera_node{};
    camera_node.id = "cam";
    camera_node.camera = SceneCameraAsset{};
    scene.nodes.push_back(std::move(camera_node));

    constexpr wz::scene::MeshHandle expected_mesh = 42;
    constexpr wz::scene::MaterialHandle expected_material = 11;

    TestRenderableResolver renderable_resolver(assets.renderables());
    TestRenderResourceResolver resource_resolver(expected_mesh, expected_material);
    SceneInstantiateContext context{
        .renderable_resolver = &renderable_resolver,
        .resource_resolver = &resource_resolver,
    };

    auto result = instantiate_scene(scene, context);
    ASSERT_TRUE(result.ok()) << "error: " << result.error_detail;

    auto& inst = result.instance;
    auto cube_h = inst.authored_to_runtime["cube"];

    EXPECT_EQ(inst.renderables[cube_h].mesh, expected_mesh);
    EXPECT_EQ(inst.renderables[cube_h].material, expected_material);

    auto cam_h = inst.authored_to_runtime["cam"];
    EXPECT_EQ(inst.renderables[cam_h].node_class.role,
        wz::scene::SceneRole::None);
}

TEST(SceneInstantiate, RejectsFailedRealization)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_realize_fail_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    using namespace wz::engine::assets;

    EngineAssetLibrary assets{ device, logger, root };

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "debug/cube",
        .kind = ProceduralMeshKind::Cube,
    });
    const auto renderable = assets.renderables().create_mesh_wireframe({
        .name = "debug/cube_wireframe",
        .mesh = mesh,
    });

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    SceneAssetData scene{};
    scene.name = "fail_realize";

    SceneNodeAsset node{};
    node.id = "obj";
    node.renderable_asset = renderable.output;
    scene.nodes.push_back(std::move(node));

    // Resolver that always fails
    class FailingResourceResolver final
        : public SceneRenderResourceResolver
    {
    public:
        bool realize_renderable_descriptor(
            const RenderableAssetData&,
            wz::scene::RenderableDescriptor&) const override
        {
            return false;
        }
    };

    TestRenderableResolver renderable_resolver(assets.renderables());
    FailingResourceResolver resource_resolver;
    SceneInstantiateContext context{
        .renderable_resolver = &renderable_resolver,
        .resource_resolver = &resource_resolver,
    };

    auto result = instantiate_scene(scene, context);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, SceneInstantiateError::RenderableRealizeFailed);
}

TEST(SceneInstantiate, MeshWithoutResourceResolverUsesPlaceholder)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_mesh_placeholder_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    using namespace wz::engine::assets;

    EngineAssetLibrary assets{ device, logger, root };

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "debug/cube",
        .kind = ProceduralMeshKind::Cube,
    });
    const auto renderable = assets.renderables().create_mesh_wireframe({
        .name = "debug/cube_wireframe",
        .mesh = mesh,
    });

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    SceneAssetData scene{};
    scene.name = "mesh_placeholder";

    SceneNodeAsset node{};
    node.id = "cube";
    node.renderable_asset = renderable.output;
    scene.nodes.push_back(std::move(node));

    TestRenderableResolver renderable_resolver(assets.renderables());
    SceneInstantiateContext context{
        .renderable_resolver = &renderable_resolver,
    };

    auto result = instantiate_scene(scene, context);
    ASSERT_TRUE(result.ok()) << "error: " << result.error_detail;

    auto cube_h = result.instance.authored_to_runtime["cube"];
    EXPECT_EQ(result.instance.renderables[cube_h].mesh, 0u);
    EXPECT_EQ(result.instance.renderables[cube_h].material, 0u);
    EXPECT_EQ(result.instance.renderables[cube_h].node_class.role,
        wz::scene::SceneRole::Renderable);
}

// ─── Issue #59: concrete mesh render resource resolver ────────────────

TEST(SceneAssetModule, ConcreteMeshResolverFlowsHandlesToDrawCommand)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_concrete_resolver_test");

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

    // The concrete resolver uses RenderResourceResolver::register_mesh()
    // to allocate a scene-render MeshHandle.
    wz::engine::rendering::RenderResourceResolver render_resolver;
    wz::engine::rendering::MeshSceneRenderResourceResolver resource_resolver(
        assets.meshes(), render_resolver);

    SceneAssetData scene{};
    scene.name = "concrete_resolver_scene";

    SceneNodeAsset node{};
    node.id = "cube_node";
    node.local.translation[0] = 4.0f;
    node.local.translation[2] = 6.0f;
    node.renderable_asset = renderable.output;
    scene.nodes.push_back(std::move(node));

    TestRenderableResolver renderable_resolver(assets.renderables());
    SceneInstantiateContext context{
        .renderable_resolver = &renderable_resolver,
        .resource_resolver = &resource_resolver,
    };

    auto result = instantiate_scene(scene, context);
    ASSERT_TRUE(result.ok()) << "error: " << result.error_detail;

    auto& inst = result.instance;
    auto cube_h = inst.authored_to_runtime["cube_node"];

    const auto& desc = inst.renderables[cube_h];
    EXPECT_EQ(desc.node_class.role, wz::scene::SceneRole::Renderable);
    EXPECT_EQ(desc.node_class.producer, wz::scene::ProducerKind::Mesh);

    // The concrete resolver registered the mesh with RenderResourceResolver,
    // so the handle should be a valid index (first registration = 0).
    EXPECT_NE(desc.mesh, wz::scene::INVALID_MESH);
    EXPECT_EQ(desc.material, wz::scene::INVALID_MATERIAL);

    // Verify the handle resolves back through RenderResourceResolver.
    auto resolved = render_resolver.resolve_mesh(desc.mesh);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_TRUE(resolved->gpu_resource.valid());

    // Full pipeline: compile → IR → frame
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
    EXPECT_EQ(compiled.scene.opaque[0].mesh, desc.mesh);
    EXPECT_EQ(compiled.scene.opaque[0].material, desc.material);

    wz::render::RenderIRStorage render_ir{};
    wz::render::build_render_ir(render_ir, compiled.scene);

    wz::render::RenderFrameStorage render_frame{};
    wz::render::build_frame(render_frame, render_ir.ir, compiled.scene);

    ASSERT_EQ(render_frame.frame.opaque.size(), 1u);

    const auto& cmd = render_frame.frame.opaque[0];
    EXPECT_EQ(cmd.mesh, desc.mesh);
    EXPECT_EQ(cmd.material, desc.material);
    EXPECT_FLOAT_EQ(cmd.world.m[12], 4.0f);
    EXPECT_FLOAT_EQ(cmd.world.m[14], 6.0f);
}

TEST(SceneInstantiate, ConcreteMeshResolverRejectsNonMeshKind)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_concrete_non_mesh_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    using namespace wz::engine::assets;

    EngineAssetLibrary assets{ device, logger, root };

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    // Create a RenderableAssetData that looks like a splat
    RenderableAssetData splat_data{};
    splat_data.kind = RenderableKind::GaussianSplatCloud;
    splat_data.source_asset.content_hash = { 0xBEEF, 0 };

    wz::engine::rendering::RenderResourceResolver render_resolver;
    wz::engine::rendering::MeshSceneRenderResourceResolver resource_resolver(
        assets.meshes(), render_resolver);

    wz::scene::RenderableDescriptor desc{};
    EXPECT_FALSE(resource_resolver.realize_renderable_descriptor(splat_data, desc));
}
