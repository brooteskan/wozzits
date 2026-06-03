// tests/asset_scene/scene_asset_module.cpp

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/scene/scene_fingerprint.h>
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

#include <algorithm>
#include <type_traits>
#include <variant>

namespace
{
#ifndef WZ_TEST_FIXTURE_DIR
#define WZ_TEST_FIXTURE_DIR "window_engine/resources"
#endif

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

    const char* kActorMovementDescriptorSceneJSON = R"({
  "schema": "wozzits.scene.v0",
  "name": "actor_movement_descriptor_test",
  "nodes": [
    {
      "id": "movable_actor",
      "transform": {
        "translation": [1, 0, 2]
      },
      "input_receiver": {
        "input_map": "asset://input_maps/actor",
        "log_input": true
      },
      "actor_movement_controller": {
        "move_speed": 7.5,
        "boost_multiplier": 2.0,
        "movement_space": "local"
      }
    }
  ]
})";

    const char* kGroundBoundaryDescriptorSceneJSON = R"({
  "schema": "wozzits.scene.v0",
  "name": "ground_boundary_descriptor_test",
  "nodes": [
    {
      "id": "terrain_surface",
      "transform": {
        "translation": [0, 0, 0]
      },
      "ground_boundary": {
        "min": [-10, 0, -8],
        "max": [12, 0, 9],
        "constrain_vertical": true,
        "enabled": true
      }
    },
    {
      "id": "movable_actor",
      "transform": {
        "translation": [1, 0, 2]
      },
      "input_receiver": {
        "input_map": "asset://input_maps/actor"
      },
      "actor_movement_controller": {
        "move_speed": 4.0,
        "boost_multiplier": 2.0,
        "movement_space": "world"
      }
    }
  ]
})";

    const char* kMeshDescriptorSceneJSON = R"({
  "schema": "wozzits.scene.v0",
  "name": "mesh_descriptor_test",
  "nodes": [
    {
      "id": "rock",
      "transform": {
        "translation": [1, 2, 3]
      },
      "mesh_source": {
        "kind": "glb",
        "path": "gltf/low_poly_rock.glb",
        "mesh_index": 1
      },
      "mesh_render_style": {
        "wireframe": {
          "enabled": true,
          "color": [0.0, 1.0, 0.2, 0.75],
          "emissive_strength": 2.5
        },
        "surface": {
          "enabled": false,
          "color": [0.25, 0.9, 0.35, 1.0],
          "emissive_strength": 0.0
        },
        "depth_test": true,
        "depth_write": true,
        "double_sided": false,
        "hidden_line_prepass": false
      }
    }
  ]
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

    const char* kAuxiliaryVisualDescriptorSceneJSON = R"({
  "schema": "wozzits.scene.v0",
  "name": "auxiliary_visual_descriptor_test",
  "nodes": [
    {
      "id": "anchor",
      "auxiliary_visual": {
        "kind": "axes",
        "scale": 1.25,
        "visible": false
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

TEST(SceneAssetModule, ActorMovementComponentDescriptorsRoundTrip)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_actor_movement_desc_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    auto rel_path = write_scene_json(
        root, "actor_movement_desc.json", kActorMovementDescriptorSceneJSON);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "actor_movement_desc",
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

    const auto& node = scene_data->nodes[0];
    ASSERT_TRUE(node.input_receiver.has_value());
    ASSERT_TRUE(node.actor_movement_controller.has_value());
    EXPECT_EQ(node.input_receiver->input_map, "asset://input_maps/actor");
    EXPECT_TRUE(node.input_receiver->log_input);
    EXPECT_FLOAT_EQ(node.actor_movement_controller->move_speed, 7.5f);
    EXPECT_FLOAT_EQ(node.actor_movement_controller->boost_multiplier, 2.0f);
    EXPECT_EQ(
        node.actor_movement_controller->movement_space,
        SceneActorMovementSpace::Local);

    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    auto& inst = result.instance;
    ASSERT_TRUE(inst.authored_to_runtime.contains("movable_actor"));
    const auto actor_h = inst.authored_to_runtime["movable_actor"];
    ASSERT_EQ(inst.input_receivers.size(), 1u);
    EXPECT_EQ(inst.input_receivers[0].node, actor_h);
    EXPECT_TRUE(inst.input_receivers[0].component.log_input);
    ASSERT_EQ(inst.actor_movement_controllers.size(), 1u);
    EXPECT_EQ(inst.actor_movement_controllers[0].node, actor_h);
    EXPECT_FLOAT_EQ(
        inst.actor_movement_controllers[0].component.move_speed,
        7.5f);
    EXPECT_FLOAT_EQ(
        inst.actor_movement_controllers[0].component.boost_multiplier,
        2.0f);
    EXPECT_EQ(
        inst.actor_movement_controllers[0].component.movement_space,
        SceneActorMovementSpace::Local);

    const auto summary = summarize_scene_instance_components(inst);
    EXPECT_EQ(summary.input_receivers, 1u);
    EXPECT_EQ(summary.actor_movement_controllers, 1u);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(
        exported.find("\"actor_movement_controller\""),
        std::string::npos);
    EXPECT_NE(exported.find("\"movement_space\""), std::string::npos);
    EXPECT_NE(exported.find("\"local\""), std::string::npos);
}

TEST(SceneAssetModule, GroundBoundaryComponentDescriptorsRoundTrip)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_ground_boundary_desc_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    auto rel_path = write_scene_json(
        root, "ground_boundary_desc.json",
        kGroundBoundaryDescriptorSceneJSON);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "ground_boundary_desc",
            .path = rel_path,
        });

    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 2u);

    const auto& surface = scene_data->nodes[0];
    ASSERT_TRUE(surface.ground_boundary.has_value());
    EXPECT_FLOAT_EQ(surface.ground_boundary->min[0], -10.0f);
    EXPECT_FLOAT_EQ(surface.ground_boundary->min[1], 0.0f);
    EXPECT_FLOAT_EQ(surface.ground_boundary->min[2], -8.0f);
    EXPECT_FLOAT_EQ(surface.ground_boundary->max[0], 12.0f);
    EXPECT_FLOAT_EQ(surface.ground_boundary->max[1], 0.0f);
    EXPECT_FLOAT_EQ(surface.ground_boundary->max[2], 9.0f);
    EXPECT_TRUE(surface.ground_boundary->constrain_vertical);
    EXPECT_TRUE(surface.ground_boundary->enabled);

    const auto& actor = scene_data->nodes[1];
    ASSERT_TRUE(actor.input_receiver.has_value());
    ASSERT_TRUE(actor.actor_movement_controller.has_value());
    EXPECT_FALSE(actor.ground_boundary.has_value());

    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    auto& inst = result.instance;
    ASSERT_TRUE(inst.authored_to_runtime.contains("terrain_surface"));
    ASSERT_TRUE(inst.authored_to_runtime.contains("movable_actor"));
    const auto surface_h = inst.authored_to_runtime["terrain_surface"];
    const auto actor_h = inst.authored_to_runtime["movable_actor"];

    ASSERT_EQ(inst.ground_boundaries.size(), 1u);
    EXPECT_EQ(inst.ground_boundaries[0].node, surface_h);
    EXPECT_FLOAT_EQ(inst.ground_boundaries[0].component.min[0], -10.0f);
    EXPECT_FLOAT_EQ(inst.ground_boundaries[0].component.max[2], 9.0f);
    EXPECT_TRUE(inst.ground_boundaries[0].component.constrain_vertical);
    EXPECT_TRUE(inst.ground_boundaries[0].component.enabled);

    ASSERT_EQ(inst.actor_movement_controllers.size(), 1u);
    EXPECT_EQ(inst.actor_movement_controllers[0].node, actor_h);

    const auto summary = summarize_scene_instance_components(inst);
    EXPECT_EQ(summary.ground_boundaries, 1u);
    EXPECT_EQ(summary.actor_movement_controllers, 1u);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"ground_boundary\""), std::string::npos);
    EXPECT_NE(exported.find("\"constrain_vertical\""), std::string::npos);
}

TEST(SceneAssetModule, MeshComponentDescriptorsRoundTrip)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_mesh_desc_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    auto rel_path = write_scene_json(
        root, "mesh_desc.json", kMeshDescriptorSceneJSON);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "mesh_desc",
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

    const auto& node = scene_data->nodes[0];
    ASSERT_TRUE(node.mesh_source.has_value());
    EXPECT_EQ(node.mesh_source->kind, SceneMeshSourceKind::GLB);
    EXPECT_EQ(node.mesh_source->path, "gltf/low_poly_rock.glb");
    EXPECT_EQ(node.mesh_source->mesh_index, 1u);

    ASSERT_TRUE(node.mesh_render_style.has_value());
    EXPECT_TRUE(node.mesh_render_style->wireframe.enabled);
    EXPECT_FALSE(node.mesh_render_style->surface.enabled);
    EXPECT_FLOAT_EQ(node.mesh_render_style->wireframe.color[1], 1.0f);
    EXPECT_FLOAT_EQ(node.mesh_render_style->wireframe.color[2], 0.2f);
    EXPECT_FLOAT_EQ(node.mesh_render_style->wireframe.color[3], 0.75f);
    EXPECT_FLOAT_EQ(
        node.mesh_render_style->wireframe.emissive_strength,
        2.5f);
    EXPECT_TRUE(node.mesh_render_style->depth_test);
    EXPECT_TRUE(node.mesh_render_style->depth_write);
    EXPECT_FALSE(node.mesh_render_style->double_sided);
    EXPECT_FALSE(node.mesh_render_style->hidden_line_prepass);

    const auto summary = summarize_authored_scene_components(*scene_data);
    EXPECT_EQ(summary.mesh_sources, 1u);
    EXPECT_EQ(summary.mesh_render_styles, 1u);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"mesh_source\""), std::string::npos);
    EXPECT_NE(exported.find("\"mesh_render_style\""), std::string::npos);
    EXPECT_NE(exported.find("\"gltf/low_poly_rock.glb\""), std::string::npos);
    EXPECT_NE(exported.find("\"color\""), std::string::npos);
    EXPECT_NE(exported.find("\"emissive_strength\""), std::string::npos);
    EXPECT_NE(exported.find("\"depth_test\""), std::string::npos);

    const wz::fs::Path reparse_root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_mesh_desc_reparse_test");

    ASSERT_EQ(
        wz::fs::create_directories(reparse_root),
        wz::fs::FileError::None);

    wz::Logger reparse_logger;
    wz::gpu::Device reparse_device{};
    wz::engine::assets::EngineAssetLibrary reparse_assets{
        reparse_device, reparse_logger, reparse_root };

    auto exported_rel_path = write_scene_json(
        reparse_root, "mesh_desc_exported.json", exported);
    const auto exported_scene_asset =
        reparse_assets.scenes().create_scene_from_json({
            .name = "mesh_desc_exported",
            .path = exported_rel_path,
        });
    ASSERT_TRUE(exported_scene_asset.valid());
    ASSERT_TRUE(reparse_assets.commit());
    ASSERT_TRUE(reparse_assets.resolve_all().ok());

    const auto* reparsed_scene_data = reparse_assets.scenes().get_scene_data(
        reparse_assets.scenes().get_scene(exported_scene_asset));
    ASSERT_NE(reparsed_scene_data, nullptr);
    ASSERT_EQ(reparsed_scene_data->nodes.size(), 1u);
    ASSERT_TRUE(reparsed_scene_data->nodes[0].mesh_source.has_value());
    ASSERT_TRUE(
        reparsed_scene_data->nodes[0].mesh_render_style.has_value());
}

TEST(SceneAssetModule, TerrainComponentRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_terrain_component_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const auto field =
        assets.scalar_fields().create_procedural_scalar_field({
            .name = "terrain/scene_height",
            .width = 4,
            .height = 4,
            .depth = 1,
            .generator = ScalarFieldGenerator::GradientX,
            .frequency = 1.0f,
            .amplitude = 1.0f,
            .format = ScalarFieldFormat::Float32,
            .domain_kind = ScalarFieldDomainKind::Spatial2D,
        });
    ASSERT_TRUE(field.valid());

    TerrainFromHeightFieldDesc terrain_desc{};
    terrain_desc.name = "terrain/scene_terrain";
    terrain_desc.height_field = field;
    terrain_desc.size[0] = 16.0f;
    terrain_desc.size[1] = 16.0f;

    const auto terrain =
        assets.terrains().create_from_height_field(terrain_desc);
    ASSERT_TRUE(terrain.valid());

    SceneAssetData authored{};
    authored.name = "terrain_component_scene";
    SceneNodeAsset node{};
    node.id = "landscape";
    node.terrain = SceneTerrainAsset{
        .terrain_asset = terrain.output,
        .visible = true,
        .queryable = true,
        .constrain_movement = false,
    };
    node.terrain_render_style = SceneTerrainRenderStyleAsset{
        .path = SceneTerrainRenderPath::DebugWireframe,
        .depth_test = true,
        .depth_write = false,
        .lighting_source = SceneTerrainLightingSource::Hybrid,
        .directional_light_node = "sun",
        .ambient_light_node = "sky_ambient",
        .environment_node = "sky_hdri",
        .ambient_strength = 0.75f,
        .sky_visibility_strength = 0.5f,
        .normal_lighting_strength = 0.9f,
        .terrain_bounce_strength = 0.2f,
    };
    authored.nodes.push_back(std::move(node));

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(authored));
    EXPECT_NE(exported.find("\"terrain\""), std::string::npos);
    EXPECT_NE(exported.find("\"terrain_render_style\""), std::string::npos);
    EXPECT_NE(exported.find("\"debug_wireframe\""), std::string::npos);
    EXPECT_NE(exported.find("\"directional_light_node\""), std::string::npos);
    EXPECT_NE(exported.find("\"ambient_light_node\""), std::string::npos);
    EXPECT_NE(exported.find("\"lighting_source\""), std::string::npos);
    EXPECT_NE(exported.find("\"hybrid\""), std::string::npos);
    EXPECT_NE(exported.find("\"environment_node\""), std::string::npos);
    EXPECT_NE(exported.find("\"terrain_bounce_strength\""), std::string::npos);
    EXPECT_NE(exported.find("\"asset\""), std::string::npos);
    EXPECT_NE(exported.find("\"queryable\""), std::string::npos);
    EXPECT_NE(exported.find("\"constrain_movement\""), std::string::npos);
    EXPECT_NE(exported.find("asset-key:"), std::string::npos);

    auto rel_path = write_scene_json(
        root, "terrain_component.scene.json", exported);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "terrain_component",
            .path = rel_path,
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);
    ASSERT_TRUE(scene_data->nodes[0].terrain.has_value());
    ASSERT_TRUE(scene_data->nodes[0].terrain_render_style.has_value());
    EXPECT_EQ(scene_data->nodes[0].terrain->terrain_asset, terrain.output);
    EXPECT_TRUE(scene_data->nodes[0].terrain->visible);
    EXPECT_TRUE(scene_data->nodes[0].terrain->queryable);
    EXPECT_FALSE(scene_data->nodes[0].terrain->constrain_movement);
    EXPECT_EQ(
        scene_data->nodes[0].terrain_render_style->path,
        SceneTerrainRenderPath::DebugWireframe);
    EXPECT_TRUE(scene_data->nodes[0].terrain_render_style->depth_test);
    EXPECT_FALSE(scene_data->nodes[0].terrain_render_style->depth_write);
    EXPECT_EQ(
        scene_data->nodes[0].terrain_render_style->lighting_source,
        SceneTerrainLightingSource::Hybrid);
    EXPECT_EQ(
        scene_data->nodes[0].terrain_render_style->directional_light_node,
        "sun");
    EXPECT_EQ(
        scene_data->nodes[0].terrain_render_style->ambient_light_node,
        "sky_ambient");
    EXPECT_EQ(
        scene_data->nodes[0].terrain_render_style->environment_node,
        "sky_hdri");
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].terrain_render_style->ambient_strength,
        0.75f);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].terrain_render_style->sky_visibility_strength,
        0.5f);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].terrain_render_style->normal_lighting_strength,
        0.9f);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].terrain_render_style->terrain_bounce_strength,
        0.2f);

    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_EQ(result.instance.terrains.size(), 1u);
    EXPECT_EQ(
        result.instance.terrains[0].component.terrain_asset,
        terrain.output);
    EXPECT_FALSE(result.instance.terrains[0].component.constrain_movement);
}

TEST(SceneAssetModule, CollisionComponentRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_collision_component_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "collision/source_cube",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const auto collision = assets.collisions().create_from_mesh({
        .name = "collision/cube_sensor",
        .mesh = mesh,
        .build_method = CollisionBuildMethod::Bounds,
        .occupancy = CollisionOccupancyData{
            .kind = CollisionOccupancyKind::Sensor,
            .blocks_movement = false,
            .queryable = true,
        },
    });
    ASSERT_TRUE(collision.valid());

    SceneAssetData authored{};
    authored.name = "collision_component_scene";
    SceneNodeAsset node{};
    node.id = "sensor";
    node.collision = SceneCollisionAsset{
        .collision_asset = collision.output,
        .layer_mask = 0x2u,
        .collides_with_mask = 0x5u,
        .is_trigger = true,
        .enabled = false,
    };
    authored.nodes.push_back(std::move(node));

    const auto authored_components =
        authored_components_for_node(authored.nodes[0]);
    EXPECT_EQ(std::count(
        authored_components.begin(),
        authored_components.end(),
        wz::scene::SceneAuthoredComponentKind::Collision), 1);

    const auto authored_summary =
        summarize_authored_scene_components(authored);
    EXPECT_EQ(authored_summary.collisions, 1u);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(authored));
    EXPECT_NE(exported.find("\"collision\""), std::string::npos);
    EXPECT_NE(exported.find("\"layer_mask\""), std::string::npos);
    EXPECT_NE(exported.find("\"collides_with_mask\""), std::string::npos);
    EXPECT_NE(exported.find("\"is_trigger\""), std::string::npos);
    EXPECT_NE(exported.find("\"enabled\""), std::string::npos);
    EXPECT_NE(exported.find("asset-key:"), std::string::npos);

    auto rel_path = write_scene_json(
        root, "collision_component.scene.json", exported);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "collision_component",
            .path = rel_path,
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);
    ASSERT_TRUE(scene_data->nodes[0].collision.has_value());
    EXPECT_EQ(
        scene_data->nodes[0].collision->collision_asset,
        collision.output);
    EXPECT_EQ(scene_data->nodes[0].collision->layer_mask, 0x2u);
    EXPECT_EQ(scene_data->nodes[0].collision->collides_with_mask, 0x5u);
    EXPECT_TRUE(scene_data->nodes[0].collision->is_trigger);
    EXPECT_FALSE(scene_data->nodes[0].collision->enabled);

    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_EQ(result.instance.collisions.size(), 1u);
    ASSERT_TRUE(result.instance.authored_to_runtime.contains("sensor"));
    EXPECT_EQ(
        result.instance.collisions[0].node,
        result.instance.authored_to_runtime["sensor"]);
    EXPECT_EQ(
        result.instance.collisions[0].component.collision_asset,
        collision.output);
    EXPECT_EQ(result.instance.collisions[0].component.layer_mask, 0x2u);
    EXPECT_EQ(result.instance.collisions[0].component.collides_with_mask, 0x5u);
    EXPECT_TRUE(result.instance.collisions[0].component.is_trigger);
    EXPECT_FALSE(result.instance.collisions[0].component.enabled);

    const auto runtime_summary =
        summarize_scene_instance_components(result.instance);
    EXPECT_EQ(runtime_summary.collisions, 1u);
}

TEST(SceneAssetModule, ProximityComponentRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_scene_proximity_roundtrip");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const std::string json = R"({
  "schema": "wozzits.scene.v0",
  "name": "proximity_scene",
  "nodes": [
    {
      "id": "sensor",
      "proximity": {
        "radius": 12.5,
        "layer_mask": 2,
        "detects_with_mask": 4,
        "enabled": true
      }
    }
  ]
})";

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const wz::fs::Path path = wz::fs::join(root, "proximity.scene.json");
    ASSERT_EQ(wz::fs::write_file_text(path, json), wz::fs::FileError::None);

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "proximity_scene",
        .path = "proximity.scene.json",
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto handle = assets.scenes().get_scene(scene_asset);
    ASSERT_TRUE(handle.valid());
    const auto* scene_data = assets.scenes().get_scene_data(handle);
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);
    ASSERT_TRUE(scene_data->nodes[0].proximity.has_value());
    EXPECT_FLOAT_EQ(scene_data->nodes[0].proximity->radius, 12.5f);
    EXPECT_EQ(scene_data->nodes[0].proximity->layer_mask, 2u);
    EXPECT_EQ(scene_data->nodes[0].proximity->detects_with_mask, 4u);
    EXPECT_TRUE(scene_data->nodes[0].proximity->enabled);

    const auto result = wz::engine::assets::instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_EQ(result.instance.proximities.size(), 1u);
    EXPECT_FLOAT_EQ(result.instance.proximities[0].component.radius, 12.5f);

    const std::string exported = wz::json::serialize_json(
        wz::engine::assets::export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"proximity\""), std::string::npos);
    EXPECT_NE(exported.find("\"detects_with_mask\""), std::string::npos);
}

TEST(SceneAssetModule, MotionComponentRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_scene_motion_roundtrip");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const std::string json = R"({
  "schema": "wozzits.scene.v0",
  "name": "motion_scene",
  "nodes": [
    {
      "id": "actor",
      "motion": {
        "linear_velocity": [1.5, -2.0, 3.25],
        "angular_velocity": [0.25, 0.5, -0.75],
        "space": "local",
        "enabled": true
      }
    }
  ]
})";

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const wz::fs::Path path = wz::fs::join(root, "motion.scene.json");
    ASSERT_EQ(wz::fs::write_file_text(path, json), wz::fs::FileError::None);

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "motion_scene",
        .path = "motion.scene.json",
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto handle = assets.scenes().get_scene(scene_asset);
    ASSERT_TRUE(handle.valid());
    const auto* scene_data = assets.scenes().get_scene_data(handle);
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);
    ASSERT_TRUE(scene_data->nodes[0].motion.has_value());
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->linear_velocity[0], 1.5f);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->linear_velocity[1], -2.0f);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->linear_velocity[2], 3.25f);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->angular_velocity[0], 0.25f);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->angular_velocity[1], 0.5f);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->angular_velocity[2], -0.75f);
    EXPECT_EQ(
        scene_data->nodes[0].motion->space,
        wz::engine::assets::SceneMotionSpace::Local);
    EXPECT_TRUE(scene_data->nodes[0].motion->enabled);

    const auto result = wz::engine::assets::instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_EQ(result.instance.motions.size(), 1u);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.linear_velocity[0],
        1.5f);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.linear_velocity[1],
        -2.0f);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.linear_velocity[2],
        3.25f);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.angular_velocity[0],
        0.25f);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.angular_velocity[1],
        0.5f);
    EXPECT_FLOAT_EQ(
        result.instance.motions[0].component.angular_velocity[2],
        -0.75f);
    EXPECT_EQ(
        result.instance.motions[0].component.space,
        wz::engine::assets::SceneMotionSpace::Local);
    EXPECT_TRUE(result.instance.motions[0].component.enabled);

    const std::string exported = wz::json::serialize_json(
        wz::engine::assets::export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"motion\""), std::string::npos);
    EXPECT_NE(exported.find("\"linear_velocity\""), std::string::npos);
    EXPECT_NE(exported.find("\"angular_velocity\""), std::string::npos);
    EXPECT_NE(exported.find("\"space\""), std::string::npos);
}

TEST(SceneAssetModule, MotionComponentDefaultsMissingLinearVelocity)
{
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_scene_motion_default_velocity");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const std::string json = R"({
  "schema": "wozzits.scene.v0",
  "name": "motion_default_scene",
  "nodes": [
    {
      "id": "actor",
      "motion": {
        "enabled": false
      }
    }
  ]
})";

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const wz::fs::Path path = wz::fs::join(root, "motion_default.scene.json");
    ASSERT_EQ(wz::fs::write_file_text(path, json), wz::fs::FileError::None);

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "motion_default_scene",
        .path = "motion_default.scene.json",
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto handle = assets.scenes().get_scene(scene_asset);
    ASSERT_TRUE(handle.valid());
    const auto* scene_data = assets.scenes().get_scene_data(handle);
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);
    ASSERT_TRUE(scene_data->nodes[0].motion.has_value());
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->linear_velocity[0], 0.0f);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->linear_velocity[1], 0.0f);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->linear_velocity[2], 0.0f);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->angular_velocity[0], 0.0f);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->angular_velocity[1], 0.0f);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].motion->angular_velocity[2], 0.0f);
    EXPECT_EQ(
        scene_data->nodes[0].motion->space,
        wz::engine::assets::SceneMotionSpace::World);
    EXPECT_FALSE(scene_data->nodes[0].motion->enabled);
}

TEST(SceneAssetModule, MotionComponentRejectsInvalidSpace)
{
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_scene_motion_invalid_space");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const std::string json = R"({
  "schema": "wozzits.scene.v0",
  "name": "motion_invalid_space_scene",
  "nodes": [
    {
      "id": "actor",
      "motion": {
        "space": "screen"
      }
    }
  ]
})";

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const wz::fs::Path path =
        wz::fs::join(root, "motion_invalid_space.scene.json");
    ASSERT_EQ(wz::fs::write_file_text(path, json), wz::fs::FileError::None);

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "motion_invalid_space_scene",
        .path = "motion_invalid_space.scene.json",
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    EXPECT_FALSE(assets.resolve_all().ok());
}

TEST(SceneAssetModule, CollisionComponentResolvesSymbolicSceneReference)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_collision_symbolic_ref_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "collision/source_cube_symbolic",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const auto collision = assets.collisions().create_from_mesh({
        .name = "collision/cube_symbolic",
        .mesh = mesh,
        .build_method = CollisionBuildMethod::Bounds,
    });
    ASSERT_TRUE(collision.valid());

    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "collision_symbolic_reference_scene",
  "nodes": [
    {
      "id": "body",
      "collision": {
        "asset": "asset://collisions/cube_symbolic",
        "layer_mask": 4,
        "collides_with_mask": 7,
        "is_trigger": false
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "collision_symbolic.scene.json", json);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "collision_symbolic",
            .path = rel_path,
            .collision_asset_references = {
                SceneAssetReferenceBinding{
                    .uri = "asset://collisions/cube_symbolic",
                    .key = collision.output,
                },
            },
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);
    ASSERT_TRUE(scene_data->nodes[0].collision.has_value());
    EXPECT_EQ(
        scene_data->nodes[0].collision->collision_asset,
        collision.output);
    EXPECT_EQ(scene_data->nodes[0].collision->layer_mask, 4u);
    EXPECT_EQ(scene_data->nodes[0].collision->collides_with_mask, 7u);
    EXPECT_FALSE(scene_data->nodes[0].collision->is_trigger);
    EXPECT_TRUE(scene_data->nodes[0].collision->enabled);

    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_EQ(result.instance.collisions.size(), 1u);
    EXPECT_EQ(
        result.instance.collisions[0].component.collision_asset,
        collision.output);
    EXPECT_EQ(result.instance.collisions[0].component.layer_mask, 4u);
    EXPECT_EQ(result.instance.collisions[0].component.collides_with_mask, 7u);
    EXPECT_FALSE(result.instance.collisions[0].component.is_trigger);
    EXPECT_TRUE(result.instance.collisions[0].component.enabled);
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

TEST(SceneDescriptorValidation, RejectsGroundBoundaryMissingBounds)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "ground_boundary_missing_bounds",
  "nodes": [{
    "id": "surface",
    "ground_boundary": { "min": [-1, 0, -1] }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("ground_boundary_missing_bounds", json));
}

TEST(SceneDescriptorValidation, RejectsGroundBoundaryInvertedBounds)
{
    const char* json = R"({
  "schema": "wozzits.scene.v0",
  "name": "ground_boundary_inverted_bounds",
  "nodes": [{
    "id": "surface",
    "ground_boundary": {
      "min": [5, 0, -1],
      "max": [-5, 0, 1]
    }
  }]
})";
    EXPECT_FALSE(scene_json_compiles("ground_boundary_inverted_bounds", json));
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

// ─── Issue #57: debug visual validation tests ───────────────────────────

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

    const auto renderable = assets.renderables().create_mesh_wireframe({
        .name = "debug/cube_wireframe",
        .mesh = mesh,
    });
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
    SceneInstantiateContext context{ .renderable_resolver = &resolver };

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

    const auto renderable = assets.renderables().create_mesh_wireframe({
        .name = "debug/cube_wireframe",
        .mesh = mesh,
    });
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
    SceneInstantiateContext context{ .renderable_resolver = &resolver };

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

    const auto renderable = assets.renderables().create_mesh_wireframe({
        .name = "debug/incremental_cube_wireframe",
        .mesh = mesh,
    });
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
    SceneInstantiateContext context{ .renderable_resolver = &resolver };
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

    MeshRenderStyleData style{};
    style.wireframe.color[0] = 1.0f;
    style.wireframe.color[1] = 0.25f;
    style.wireframe.color[2] = 0.0f;
    style.wireframe.color[3] = 1.0f;
    style.wireframe.emissive_strength = 1.5f;

    const auto render_style =
        assets.mesh_render_styles().create_mesh_render_style({
            .name = "styles/orange_wire",
            .style = style,
        });
    ASSERT_TRUE(render_style.valid());

    const auto renderable = assets.renderables().create_mesh_styled({
        .name = "debug/cube_wireframe",
        .mesh = mesh,
        .style = render_style,
    });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());
    const auto renderable_handle =
        assets.renderables().get_renderable(renderable);
    ASSERT_TRUE(renderable_handle.valid());
    const auto* renderable_data =
        assets.renderables().get_renderable_data(renderable_handle);
    ASSERT_NE(renderable_data, nullptr);
    EXPECT_FLOAT_EQ(renderable_data->mesh_style.wireframe.color[0], 1.0f);
    EXPECT_FLOAT_EQ(renderable_data->mesh_style.wireframe.color[1], 0.25f);
    EXPECT_FLOAT_EQ(
        renderable_data->mesh_style.wireframe.emissive_strength,
        1.5f);

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
    EXPECT_FLOAT_EQ(resolved->mesh_style.wireframe.color[0], 1.0f);
    EXPECT_FLOAT_EQ(resolved->mesh_style.wireframe.color[1], 0.25f);
    EXPECT_FLOAT_EQ(
        resolved->mesh_style.wireframe.emissive_strength,
        1.5f);

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

TEST(SceneInstantiate, ConcreteMeshResolverRejectsSplatRenderableDuringInstantiation)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_concrete_splat_unsupported_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    using namespace wz::engine::assets;

    EngineAssetLibrary assets{ device, logger, root };

    const auto cloud =
        assets.gaussian_splats().create_procedural_cloud({
            .name = "debug/splat_sphere",
            .count = 64,
            .radius = 2.0f,
            .splat_scale = 1.0f,
        });
    ASSERT_TRUE(cloud.valid());

    const auto renderable =
        assets.renderables().create_gaussian_splat_debug({
            .name = "debug/splat_sphere_debug",
            .splat_cloud = cloud,
        });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    wz::engine::rendering::RenderResourceResolver render_resolver;
    wz::engine::rendering::MeshSceneRenderResourceResolver resource_resolver(
        assets.meshes(), render_resolver);

    SceneAssetData scene{};
    scene.name = "splat_unsupported_scene";

    SceneNodeAsset node{};
    node.id = "splat_node";
    node.renderable_asset = renderable.output;
    scene.nodes.push_back(std::move(node));

    TestRenderableResolver renderable_resolver(assets.renderables());
    SceneInstantiateContext context{
        .renderable_resolver = &renderable_resolver,
        .resource_resolver = &resource_resolver,
    };

    auto result = instantiate_scene(scene, context);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, SceneInstantiateError::RenderableRealizeFailed);
}

TEST(SceneInstantiate, ConcreteMeshResolverRejectsScalarFieldRenderableDuringInstantiation)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_concrete_scalar_unsupported_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    using namespace wz::engine::assets;

    EngineAssetLibrary assets{ device, logger, root };

    const auto field =
        assets.scalar_fields().create_procedural_scalar_field({
            .name = "debug/scalar_gradient",
            .width = 16,
            .height = 16,
            .depth = 1,
            .generator = ScalarFieldGenerator::GradientX,
            .frequency = 1.0f,
            .amplitude = 1.0f,
            .format = ScalarFieldFormat::Float32,
            .domain_kind = ScalarFieldDomainKind::Spatial2D,
        });
    ASSERT_TRUE(field.valid());

    const auto renderable =
        assets.renderables().create_scalar_field_debug({
            .name = "debug/scalar_gradient_debug",
            .scalar_field = field,
        });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    wz::engine::rendering::RenderResourceResolver render_resolver;
    wz::engine::rendering::MeshSceneRenderResourceResolver resource_resolver(
        assets.meshes(), render_resolver);

    SceneAssetData scene{};
    scene.name = "scalar_unsupported_scene";

    SceneNodeAsset node{};
    node.id = "scalar_node";
    node.renderable_asset = renderable.output;
    scene.nodes.push_back(std::move(node));

    TestRenderableResolver renderable_resolver(assets.renderables());
    SceneInstantiateContext context{
        .renderable_resolver = &renderable_resolver,
        .resource_resolver = &resource_resolver,
    };

    auto result = instantiate_scene(scene, context);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, SceneInstantiateError::RenderableRealizeFailed);
}

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

    add_scene_node(scene, std::move(node));

    const auto summary = summarize_authored_scene_components(scene);
    EXPECT_EQ(summary.nodes, 1u);
    EXPECT_EQ(summary.transforms, 1u);
    EXPECT_EQ(summary.renderables, 1u);
    EXPECT_EQ(summary.collisions, 1u);
    EXPECT_EQ(summary.cameras, 1u);
    EXPECT_EQ(summary.auxiliary_visuals, 1u);
    EXPECT_EQ(summary.editor_handles, 1u);

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

TEST(SceneECSBoundary, AuthoredIdsMapToRuntimeEntitiesAndBack)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "ecs_identity";

    SceneNodeAsset root{};
    root.id = "root";
    scene.nodes.push_back(std::move(root));

    SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "root";
    child.camera = SceneCameraAsset{};
    scene.nodes.push_back(std::move(child));

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const auto& inst = result.instance;
    ASSERT_TRUE(inst.authored_to_runtime.contains("root"));
    ASSERT_TRUE(inst.authored_to_runtime.contains("child"));

    const wz::scene::RuntimeEntityId root_entity =
        inst.authored_to_runtime.at("root");
    const wz::scene::RuntimeEntityId child_entity =
        inst.authored_to_runtime.at("child");

    EXPECT_NE(root_entity, wz::scene::INVALID_RUNTIME_ENTITY);
    EXPECT_NE(child_entity, wz::scene::INVALID_RUNTIME_ENTITY);
    ASSERT_LT(root_entity, inst.runtime_to_authored.size());
    ASSERT_LT(child_entity, inst.runtime_to_authored.size());
    EXPECT_EQ(inst.runtime_to_authored[root_entity], "root");
    EXPECT_EQ(inst.runtime_to_authored[child_entity], "child");
}

TEST(SceneECSBoundary, SceneECSVocabularyIsSceneLayerOnly)
{
    static_assert(std::is_same_v<
        wz::scene::AuthoredEntityId,
        std::string>);
    static_assert(std::is_same_v<
        wz::scene::RuntimeEntityId,
        wz::core::graph::NodeHandle>);
    static_assert(std::is_same_v<
        decltype(wz::scene::RuntimeComponentRecord<int>{}.node),
        wz::scene::RuntimeEntityId>);

    wz::scene::RuntimeComponentRecord<int> record{};
    record.node = wz::scene::INVALID_RUNTIME_ENTITY;
    record.component = 7;

    EXPECT_EQ(record.node, wz::scene::INVALID_RUNTIME_ENTITY);
    EXPECT_EQ(record.component, 7);
    EXPECT_TRUE(wz::scene::is_runtime_relevant_component(
        wz::scene::SceneAuthoredComponentKind::GroundBoundary));
    EXPECT_TRUE(wz::scene::is_runtime_relevant_component(
        wz::scene::SceneAuthoredComponentKind::Collision));
    EXPECT_TRUE(wz::scene::is_runtime_relevant_component(
        wz::scene::SceneAuthoredComponentKind::Proximity));
    EXPECT_TRUE(wz::scene::is_runtime_relevant_component(
        wz::scene::SceneAuthoredComponentKind::Motion));
    EXPECT_TRUE(wz::scene::is_runtime_relevant_component(
        wz::scene::SceneAuthoredComponentKind::Behavior));
}

TEST(SceneECSBoundary, EmptySceneSummaryIsZeroed)
{
    const wz::engine::assets::SceneAssetData scene{};

    const auto summary =
        wz::engine::assets::summarize_authored_scene_components(scene);

    EXPECT_EQ(summary.nodes, 0u);
    EXPECT_EQ(summary.transforms, 0u);
    EXPECT_EQ(summary.visibility, 0u);
    EXPECT_EQ(summary.motion_types, 0u);
    EXPECT_EQ(summary.parent_links, 0u);
    EXPECT_EQ(summary.renderables, 0u);
    EXPECT_EQ(summary.cameras, 0u);
    EXPECT_EQ(summary.lights, 0u);
    EXPECT_EQ(summary.input_receivers, 0u);
    EXPECT_EQ(summary.flying_camera_controllers, 0u);
    EXPECT_EQ(summary.actor_movement_controllers, 0u);
    EXPECT_EQ(summary.ground_boundaries, 0u);
    EXPECT_EQ(summary.collisions, 0u);
    EXPECT_EQ(summary.proximities, 0u);
    EXPECT_EQ(summary.motions, 0u);
    EXPECT_EQ(summary.behaviors, 0u);
    EXPECT_EQ(summary.audio_listeners, 0u);
    EXPECT_EQ(summary.event_listeners, 0u);
    EXPECT_EQ(summary.auxiliary_visuals, 0u);
    EXPECT_EQ(summary.editor_handles, 0u);
}

TEST(SceneECSBoundary, EmptyRuntimeSummaryIsZeroed)
{
    const wz::engine::assets::SceneInstance instance{};

    const auto summary =
        wz::engine::assets::summarize_scene_instance_components(instance);

    EXPECT_EQ(summary.runtime_entities, 0u);
    EXPECT_EQ(summary.renderable_descriptor_slots, 0u);
    EXPECT_EQ(summary.lights, 0u);
    EXPECT_EQ(summary.input_receivers, 0u);
    EXPECT_EQ(summary.flying_camera_controllers, 0u);
    EXPECT_EQ(summary.actor_movement_controllers, 0u);
    EXPECT_EQ(summary.ground_boundaries, 0u);
    EXPECT_EQ(summary.collisions, 0u);
    EXPECT_EQ(summary.proximities, 0u);
    EXPECT_EQ(summary.motions, 0u);
    EXPECT_EQ(summary.behaviors, 0u);
    EXPECT_EQ(summary.audio_listeners, 0u);
    EXPECT_EQ(summary.event_listeners, 0u);
    EXPECT_EQ(summary.auxiliary_visuals, 0u);
    EXPECT_EQ(summary.editor_handles, 0u);
}

TEST(SceneECSBoundary, CoreNodeFieldsDoNotCountAsOptionalComponents)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "core_only";

    SceneNodeAsset root{};
    root.id = "root";
    scene.nodes.push_back(std::move(root));

    SceneNodeAsset child{};
    child.id = "child";
    child.parent_id = "root";
    child.local.translation[0] = 1.0f;
    child.visible = false;
    child.motion_type = wz::scene::TransformNode::MotionType::Animated;

    EXPECT_FALSE(has_authored_renderable_component(child));
    EXPECT_FALSE(has_authored_camera_component(child));
    EXPECT_FALSE(has_authored_editor_only_components(child));
    EXPECT_FALSE(has_authored_auxiliary_visual_component(child));
    EXPECT_FALSE(has_authored_debug_visual_component(child));
    EXPECT_FALSE(has_runtime_relevant_components(child));
    scene.nodes.push_back(std::move(child));

    const auto summary = summarize_authored_scene_components(scene);
    EXPECT_EQ(summary.nodes, 2u);
    EXPECT_EQ(summary.transforms, 2u);
    EXPECT_EQ(summary.visibility, 2u);
    EXPECT_EQ(summary.motion_types, 2u);
    EXPECT_EQ(summary.parent_links, 1u);
    EXPECT_EQ(summary.renderables, 0u);
    EXPECT_EQ(summary.cameras, 0u);
    EXPECT_EQ(summary.input_receivers, 0u);
    EXPECT_EQ(summary.flying_camera_controllers, 0u);
    EXPECT_EQ(summary.actor_movement_controllers, 0u);
    EXPECT_EQ(summary.ground_boundaries, 0u);
    EXPECT_EQ(summary.collisions, 0u);
    EXPECT_EQ(summary.proximities, 0u);
    EXPECT_EQ(summary.motions, 0u);
    EXPECT_EQ(summary.behaviors, 0u);
    EXPECT_EQ(summary.audio_listeners, 0u);
    EXPECT_EQ(summary.event_listeners, 0u);
    EXPECT_EQ(summary.auxiliary_visuals, 0u);
    EXPECT_EQ(summary.editor_handles, 0u);
}

TEST(SceneECSBoundary, SummarizesAuthoredComponentInventory)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "component_inventory";

    SceneNodeAsset render_node{};
    render_node.id = "render_node";
    render_node.renderable = SceneRenderableBinding{};
    scene.nodes.push_back(std::move(render_node));

    SceneNodeAsset camera_node{};
    camera_node.id = "camera_node";
    camera_node.parent_id = "render_node";
    camera_node.camera = SceneCameraAsset{};
    camera_node.input_receiver = SceneInputReceiverAsset{
        .input_map = "asset://input_maps/editor",
    };
    camera_node.flying_camera_controller =
        SceneFlyingCameraControllerAsset{};
    camera_node.actor_movement_controller =
        SceneActorMovementControllerAsset{};
    camera_node.ground_boundary = SceneGroundBoundaryAsset{
        .min = { -5.0f, 0.0f, -5.0f },
        .max = { 5.0f, 0.0f, 5.0f },
    };
    camera_node.collision = SceneCollisionAsset{
        .layer_mask = 0x8u,
    };
    camera_node.audio_listener = SceneAudioListenerAsset{};
    camera_node.event_listener = SceneEventListenerAsset{
        .channels = { "editor" },
    };
    camera_node.proximity = SceneProximityAsset{
        .radius = 2.0f,
    };
    camera_node.motion = SceneMotionAsset{
        .linear_velocity = { 1.0f, 0.0f, 0.0f },
    };
    camera_node.behavior = SceneBehaviorAsset{
        .module = "gameplay",
        .name = "tick",
    };
    camera_node.debug_visual = SceneDebugVisualAsset{
        .kind = SceneDebugVisualKind::Axes,
    };
    camera_node.editor_handle = SceneEditorHandleAsset{};

    EXPECT_TRUE(has_authored_camera_component(camera_node));
    EXPECT_TRUE(has_authored_editor_only_components(camera_node));
    EXPECT_TRUE(has_authored_auxiliary_visual_component(camera_node));
    EXPECT_TRUE(has_authored_debug_visual_component(camera_node));
    EXPECT_TRUE(has_runtime_relevant_components(camera_node));
    const auto components = authored_components_for_node(camera_node);
    EXPECT_NE(
        std::find(
            components.begin(),
            components.end(),
            wz::scene::SceneAuthoredComponentKind::Proximity),
        components.end());
    EXPECT_NE(
        std::find(
            components.begin(),
            components.end(),
            wz::scene::SceneAuthoredComponentKind::Motion),
        components.end());
    EXPECT_NE(
        std::find(
            components.begin(),
            components.end(),
            wz::scene::SceneAuthoredComponentKind::Behavior),
        components.end());
    scene.nodes.push_back(std::move(camera_node));

    scene.lights.push_back(SceneLightAsset{ .node_id = "render_node" });

    const auto summary = summarize_authored_scene_components(scene);
    EXPECT_EQ(summary.nodes, 2u);
    EXPECT_EQ(summary.transforms, 2u);
    EXPECT_EQ(summary.visibility, 2u);
    EXPECT_EQ(summary.motion_types, 2u);
    EXPECT_EQ(summary.parent_links, 1u);
    EXPECT_EQ(summary.renderables, 1u);
    EXPECT_EQ(summary.cameras, 1u);
    EXPECT_EQ(summary.lights, 1u);
    EXPECT_EQ(summary.input_receivers, 1u);
    EXPECT_EQ(summary.flying_camera_controllers, 1u);
    EXPECT_EQ(summary.actor_movement_controllers, 1u);
    EXPECT_EQ(summary.ground_boundaries, 1u);
    EXPECT_EQ(summary.collisions, 1u);
    EXPECT_EQ(summary.proximities, 1u);
    EXPECT_EQ(summary.motions, 1u);
    EXPECT_EQ(summary.behaviors, 1u);
    EXPECT_EQ(summary.audio_listeners, 1u);
    EXPECT_EQ(summary.event_listeners, 1u);
    EXPECT_EQ(summary.auxiliary_visuals, 1u);
    EXPECT_EQ(summary.editor_handles, 1u);
}

TEST(SceneECSBoundary, CountsLegacyAndAssetBackedRenderableComponents)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "mixed_renderable_component_sources";

    SceneNodeAsset legacy_node{};
    legacy_node.id = "legacy_renderable";
    legacy_node.renderable = SceneRenderableBinding{};
    EXPECT_TRUE(has_authored_renderable_component(legacy_node));
    scene.nodes.push_back(std::move(legacy_node));

    SceneNodeAsset asset_node{};
    asset_node.id = "asset_renderable";
    wz::asset::AssetKey renderable_key{};
    renderable_key.content_hash = { 0x58, 0x01 };
    asset_node.renderable_asset = renderable_key;
    EXPECT_TRUE(has_authored_renderable_component(asset_node));
    scene.nodes.push_back(std::move(asset_node));

    SceneNodeAsset empty_node{};
    empty_node.id = "empty";
    EXPECT_FALSE(has_authored_renderable_component(empty_node));
    scene.nodes.push_back(std::move(empty_node));

    const auto summary = summarize_authored_scene_components(scene);
    EXPECT_EQ(summary.nodes, 3u);
    EXPECT_EQ(summary.renderables, 2u);
}

TEST(SceneECSBoundary, AssetBackedRenderableDoesNotEmbedAssetDefinition)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "asset_reference_boundary";

    wz::asset::AssetKey renderable_key{};
    renderable_key.content_hash = { 0x7100, 0x01 };
    renderable_key.schema_hash = { 0x7100, 0x02 };

    SceneNodeAsset node{};
    node.id = "landscape_or_actor_visual";
    node.renderable_asset = renderable_key;
    scene.nodes.push_back(node);

    const auto components = authored_components_for_node(scene.nodes[0]);
    EXPECT_EQ(std::count(
        components.begin(),
        components.end(),
        wz::scene::SceneAuthoredComponentKind::Renderable), 1);

    EXPECT_TRUE(has_authored_renderable_component(scene.nodes[0]));
    EXPECT_FALSE(scene.nodes[0].renderable.has_value());
    ASSERT_TRUE(scene.nodes[0].renderable_asset.has_value());
    EXPECT_EQ(*scene.nodes[0].renderable_asset, renderable_key);

    const auto summary = summarize_authored_scene_components(scene);
    EXPECT_EQ(summary.nodes, 1u);
    EXPECT_EQ(summary.renderables, 1u);
    EXPECT_EQ(summary.cameras, 0u);
    EXPECT_EQ(summary.input_receivers, 0u);
    EXPECT_EQ(summary.actor_movement_controllers, 0u);
    EXPECT_EQ(summary.ground_boundaries, 0u);
    EXPECT_EQ(summary.editor_handles, 0u);
}

TEST(SceneECSBoundary, RuntimeReadySceneUsesAssetReferencesWithoutRecipes)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_runtime_ready_refs_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    EngineAssetLibrary assets{ device, logger, root };
    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "runtime_ready/cube",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const auto renderable = assets.renderables().create_mesh_wireframe({
        .name = "runtime_ready/cube_wireframe",
        .mesh = mesh,
    });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    SceneAssetData scene{};
    scene.name = "runtime_ready_asset_refs";

    wz::asset::AssetKey terrain_key{};
    terrain_key.content_hash = { 0x7300, 0x01 };
    terrain_key.schema_hash = { 0x7300, 0x02 };

    SceneNodeAsset visual{};
    visual.id = "visual";
    visual.renderable_asset = renderable.output;
    scene.nodes.push_back(std::move(visual));

    SceneNodeAsset terrain{};
    terrain.id = "terrain";
    terrain.terrain = SceneTerrainAsset{
        .terrain_asset = terrain_key,
        .visible = true,
        .queryable = true,
        .constrain_movement = true,
    };
    scene.nodes.push_back(std::move(terrain));

    EXPECT_FALSE(has_asset_authoring_recipes(scene.nodes[0]));
    EXPECT_FALSE(has_asset_authoring_recipes(scene.nodes[1]));

    const auto recipe_summary = summarize_scene_asset_authoring_recipes(scene);
    EXPECT_EQ(recipe_summary.nodes_with_recipes, 0u);
    EXPECT_EQ(recipe_summary.total_recipes, 0u);

    const auto authored_summary = summarize_authored_scene_components(scene);
    EXPECT_EQ(authored_summary.renderables, 1u);
    EXPECT_EQ(authored_summary.terrains, 1u);
    EXPECT_EQ(authored_summary.mesh_sources, 0u);
    EXPECT_EQ(authored_summary.scalar_field_sources, 0u);
    EXPECT_EQ(authored_summary.vector_field_sources, 0u);
    EXPECT_EQ(authored_summary.terrain_mesh_sources, 0u);
    EXPECT_EQ(authored_summary.terrain_height_field_sources, 0u);

    TestRenderableResolver renderable_resolver(assets.renderables());
    SceneInstantiateContext context{
        .renderable_resolver = &renderable_resolver,
    };
    auto result = instantiate_scene(scene, context);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const auto runtime_summary =
        summarize_scene_instance_components(result.instance);
    EXPECT_EQ(runtime_summary.runtime_entities, 2u);
    EXPECT_EQ(runtime_summary.terrains, 1u);
    EXPECT_EQ(runtime_summary.terrain_mesh_sources, 0u);
    EXPECT_EQ(runtime_summary.terrain_height_field_sources, 0u);
    EXPECT_TRUE(result.instance.terrains[0].component.terrain_asset
        == terrain_key);
}

TEST(SceneECSBoundary, TerrainSourceCandidateHelpersMatchEditorRules)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "terrain_source_candidates";

    SceneNodeAsset terrain = make_scene_node("terrain");
    terrain.terrain = SceneTerrainAsset{};
    scene.nodes.push_back(std::move(terrain));

    SceneNodeAsset child_mesh = make_scene_node("child_mesh");
    child_mesh.parent_id = "terrain";
    child_mesh.mesh_source = SceneMeshSourceAsset{};
    scene.nodes.push_back(std::move(child_mesh));

    SceneNodeAsset child_field = make_scene_node("child_field");
    child_field.parent_id = "terrain";
    child_field.scalar_field_source = SceneScalarFieldSourceAsset{};
    scene.nodes.push_back(std::move(child_field));

    SceneNodeAsset sibling_mesh = make_scene_node("sibling_mesh");
    sibling_mesh.mesh_source = SceneMeshSourceAsset{};
    scene.nodes.push_back(std::move(sibling_mesh));

    const auto* terrain_node = find_scene_node(scene, "terrain");
    ASSERT_NE(terrain_node, nullptr);

    const auto mesh_candidates =
        terrain_mesh_source_candidate_nodes(scene, *terrain_node);
    ASSERT_EQ(mesh_candidates.size(), 1u);
    EXPECT_EQ(mesh_candidates[0], "child_mesh");

    const auto height_candidates =
        terrain_height_field_source_candidate_nodes(scene, *terrain_node);
    ASSERT_EQ(height_candidates.size(), 1u);
    EXPECT_EQ(height_candidates[0], "child_field");

    EXPECT_TRUE(is_terrain_mesh_source_node_candidate(
        *terrain_node,
        scene.nodes[1]));
    EXPECT_FALSE(is_terrain_mesh_source_node_candidate(
        *terrain_node,
        scene.nodes[3]));
}

TEST(SceneECSBoundary, ExclusiveTerrainSourceAttachHelpersClearOppositeSource)
{
    using namespace wz::engine::assets;

    SceneNodeAsset node = make_scene_node("terrain");
    attach_terrain_mesh_source(node, SceneTerrainMeshSourceAsset{});
    attach_exclusive_terrain_height_field_source(
        node,
        SceneTerrainHeightFieldSourceAsset{});

    EXPECT_FALSE(node.terrain_mesh_source.has_value());
    EXPECT_TRUE(node.terrain_height_field_source.has_value());

    attach_exclusive_terrain_mesh_source(node, SceneTerrainMeshSourceAsset{});

    EXPECT_TRUE(node.terrain_mesh_source.has_value());
    EXPECT_FALSE(node.terrain_height_field_source.has_value());
}

TEST(SceneECSBoundary, AuthoredLightDirectionUsesNodeLocalNegativeY)
{
    using namespace wz::engine::assets;

    SceneNodeAsset node = make_scene_node("sun");

    float direction[3]{};
    authored_light_direction_from_node(node, direction);
    EXPECT_FLOAT_EQ(direction[0], 0.0f);
    EXPECT_FLOAT_EQ(direction[1], -1.0f);
    EXPECT_FLOAT_EQ(direction[2], 0.0f);

    const float target[3]{ 0.0f, 0.0f, 1.0f };
    set_node_rotation_from_authored_light_direction(node, target);
    authored_light_direction_from_node(node, direction);

    EXPECT_NEAR(direction[0], target[0], 1e-5f);
    EXPECT_NEAR(direction[1], target[1], 1e-5f);
    EXPECT_NEAR(direction[2], target[2], 1e-5f);
}

TEST(SceneECSBoundary, DetachesStaleMaterializedRenderables)
{
    using namespace wz::engine::assets;

    wz::asset::AssetKey renderable_key{};
    renderable_key.content_hash = { 0x9898, 0x1234 };

    SceneAssetData scene{};
    scene.name = "detach_stale_materialized_renderables";

    SceneNodeAsset terrain = make_scene_node("terrain");
    terrain.terrain = SceneTerrainAsset{};
    terrain.terrain_mesh_source = SceneTerrainMeshSourceAsset{
        .mode = SceneTerrainMeshSourceMode::SceneNode,
        .source_node = "source_mesh",
    };
    terrain.renderable_asset = renderable_key;
    scene.nodes.push_back(std::move(terrain));

    SceneNodeAsset source = make_scene_node("source_mesh");
    source.parent_id = "terrain";
    source.mesh_source = SceneMeshSourceAsset{};
    source.renderable_asset = renderable_key;
    scene.nodes.push_back(std::move(source));

    SceneNodeAsset stale = make_scene_node("empty");
    stale.renderable_asset = renderable_key;
    scene.nodes.push_back(std::move(stale));

    EXPECT_TRUE(can_own_materialized_renderable_asset(scene, scene.nodes[0]));
    EXPECT_FALSE(can_own_materialized_renderable_asset(scene, scene.nodes[1]));
    EXPECT_FALSE(can_own_materialized_renderable_asset(scene, scene.nodes[2]));

    EXPECT_EQ(detach_stale_materialized_renderable_assets(scene), 2u);
    EXPECT_TRUE(scene.nodes[0].renderable_asset.has_value());
    EXPECT_FALSE(scene.nodes[1].renderable_asset.has_value());
    EXPECT_FALSE(scene.nodes[2].renderable_asset.has_value());
}

TEST(SceneECSBoundary, SummarizesRuntimeProjectionInventory)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "runtime_inventory";

    SceneNodeAsset root{};
    root.id = "root";
    scene.nodes.push_back(std::move(root));

    SceneNodeAsset camera{};
    camera.id = "camera";
    camera.parent_id = "root";
    camera.camera = SceneCameraAsset{};
    camera.input_receiver = SceneInputReceiverAsset{
        .input_map = "asset://input_maps/editor",
    };
    camera.flying_camera_controller =
        SceneFlyingCameraControllerAsset{};
    camera.actor_movement_controller =
        SceneActorMovementControllerAsset{};
    camera.ground_boundary = SceneGroundBoundaryAsset{
        .min = { -5.0f, 0.0f, -5.0f },
        .max = { 5.0f, 0.0f, 5.0f },
    };
    camera.collision = SceneCollisionAsset{
        .layer_mask = 0x10u,
    };
    camera.audio_listener = SceneAudioListenerAsset{};
    camera.event_listener = SceneEventListenerAsset{
        .channels = { "editor" },
    };
    camera.debug_visual = SceneDebugVisualAsset{
        .kind = SceneDebugVisualKind::Axes,
    };
    camera.editor_handle = SceneEditorHandleAsset{};
    scene.nodes.push_back(std::move(camera));

    scene.lights.push_back(SceneLightAsset{ .node_id = "camera" });

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const auto summary = summarize_scene_instance_components(result.instance);
    EXPECT_EQ(summary.runtime_entities, 2u);
    EXPECT_EQ(summary.renderable_descriptor_slots, 2u);
    EXPECT_EQ(summary.lights, 1u);
    EXPECT_EQ(summary.input_receivers, 1u);
    EXPECT_EQ(summary.flying_camera_controllers, 1u);
    EXPECT_EQ(summary.actor_movement_controllers, 1u);
    EXPECT_EQ(summary.ground_boundaries, 1u);
    EXPECT_EQ(summary.collisions, 1u);
    EXPECT_EQ(summary.audio_listeners, 1u);
    EXPECT_EQ(summary.event_listeners, 1u);
    EXPECT_EQ(summary.auxiliary_visuals, 1u);
    EXPECT_EQ(summary.editor_handles, 1u);
}

TEST(SceneECSBoundary, SummaryCountsDeclaredLightsWithoutResolvingNodeIds)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "declared_lights";

    SceneNodeAsset node{};
    node.id = "real_node";
    scene.nodes.push_back(std::move(node));

    scene.lights.push_back(SceneLightAsset{ .node_id = "real_node" });
    scene.lights.push_back(SceneLightAsset{ .node_id = "missing_node" });

    const auto summary = summarize_authored_scene_components(scene);
    EXPECT_EQ(summary.nodes, 1u);
    EXPECT_EQ(summary.lights, 2u);
}

TEST(SceneECSBoundary, DuplicateAuthoredIdsAreRejected)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "duplicate_ids";

    SceneNodeAsset first{};
    first.id = "dup";
    scene.nodes.push_back(std::move(first));

    SceneNodeAsset second{};
    second.id = "dup";
    scene.nodes.push_back(std::move(second));

    auto result = instantiate_scene(scene);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, SceneInstantiateError::DuplicateNodeId);
    EXPECT_NE(result.error_detail.find("id='dup'"), std::string::npos);
    EXPECT_NE(result.error_detail.find("name=''"), std::string::npos);
}

TEST(SceneECSBoundary, FingerprintTracksAuthoredComponentData)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "fingerprint_scene";

    SceneNodeAsset node{};
    node.id = "camera";
    node.camera = SceneCameraAsset{};
    scene.nodes.push_back(std::move(node));

    const uint64_t original = scene_asset_fingerprint(scene);

    scene.nodes[0].camera->fov_y = 0.75f;
    const uint64_t changed = scene_asset_fingerprint(scene);

    EXPECT_NE(original, changed);
}

TEST(SceneECSBoundary, FingerprintTracksCollisionComponentData)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "fingerprint_collision_scene";

    wz::asset::AssetKey collision_key{};
    collision_key.content_hash = { 0xc011, 0x510u };

    SceneNodeAsset node{};
    node.id = "body";
    node.collision = SceneCollisionAsset{
        .collision_asset = collision_key,
        .layer_mask = 0x2u,
        .collides_with_mask = 0x4u,
    };
    scene.nodes.push_back(std::move(node));

    const uint64_t original = scene_asset_fingerprint(scene);

    scene.nodes[0].collision->collides_with_mask = 0x8u;
    const uint64_t changed = scene_asset_fingerprint(scene);

    EXPECT_NE(original, changed);
}

TEST(SceneECSBoundary, FingerprintTracksMotionComponentData)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "fingerprint_motion_scene";

    SceneNodeAsset node{};
    node.id = "mover";
    node.motion = SceneMotionAsset{
        .linear_velocity = { 1.0f, 0.0f, 0.0f },
    };
    scene.nodes.push_back(std::move(node));

    const uint64_t original = scene_asset_fingerprint(scene);

    scene.nodes[0].motion->angular_velocity[1] = 0.5f;
    const uint64_t angular_changed = scene_asset_fingerprint(scene);
    EXPECT_NE(original, angular_changed);

    scene.nodes[0].motion->angular_velocity[1] = 0.0f;
    scene.nodes[0].motion->space = SceneMotionSpace::Local;
    const uint64_t space_changed = scene_asset_fingerprint(scene);
    EXPECT_NE(original, space_changed);
}

TEST(SceneECSBoundary, FingerprintTracksEditorAuthoringDrafts)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "fingerprint_authoring_draft_scene";

    wz::asset::AssetKey mesh_key{};
    mesh_key.content_hash = { 0x1111, 0x2222 };
    wz::asset::AssetKey scalar_key{};
    scalar_key.content_hash = { 0x3333, 0x4444 };
    wz::asset::AssetKey vector_key{};
    vector_key.content_hash = { 0x5555, 0x6666 };

    SceneNodeAsset node{};
    node.id = "rock";
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::GLB,
        .path = "gltf/low_poly_rock.glb",
        .mesh_index = 0,
    };
    node.mesh_render_style = SceneMeshRenderStyleAsset{
        .depth_test = true,
        .depth_write = false,
    };
    node.scalar_field_source = SceneScalarFieldSourceAsset{
        .kind = SceneScalarFieldSourceKind::ProceduralSineWaves,
        .scalar_field_asset = scalar_key,
        .width = 32,
        .height = 16,
        .frequency = 2.0f,
        .amplitude = 0.5f,
    };
    node.vector_field_source = SceneVectorFieldSourceAsset{
        .kind = SceneVectorFieldSourceKind::RawF32,
        .vector_field_asset = vector_key,
        .path = "fields/normal.raw",
        .width = 32,
        .height = 16,
        .components_per_channel = 3,
        .channels = { VectorFieldChannelDesc{ .name = "normal" } },
    };
    node.terrain_render_style = SceneTerrainRenderStyleAsset{
        .path = SceneTerrainRenderPath::Surface,
        .depth_test = true,
        .depth_write = true,
    };
    node.terrain_mesh_source = SceneTerrainMeshSourceAsset{
        .mode = SceneTerrainMeshSourceMode::MeshAsset,
        .mesh_asset = mesh_key,
        .min_surface_normal_y = 0.4f,
        .include_backfaces = true,
    };
    node.terrain_height_field_source = SceneTerrainHeightFieldSourceAsset{
        .mode = SceneTerrainHeightFieldSourceMode::ScalarFieldAsset,
        .scalar_field_asset = scalar_key,
        .origin = { -1.0f, -2.0f },
        .size = { 8.0f, 9.0f },
        .vertical_scale = 3.0f,
        .base_height = -0.25f,
    };
    scene.nodes.push_back(std::move(node));

    const uint64_t original = scene_asset_fingerprint(scene);

    scene.nodes[0].mesh_source->mesh_index = 1;
    EXPECT_NE(original, scene_asset_fingerprint(scene));
    scene.nodes[0].mesh_source->mesh_index = 0;

    scene.nodes[0].mesh_render_style->depth_write = true;
    EXPECT_NE(original, scene_asset_fingerprint(scene));
    scene.nodes[0].mesh_render_style->depth_write = false;

    scene.nodes[0].scalar_field_source->amplitude = 1.0f;
    EXPECT_NE(original, scene_asset_fingerprint(scene));
    scene.nodes[0].scalar_field_source->amplitude = 0.5f;

    scene.nodes[0].vector_field_source->components_per_channel = 2;
    EXPECT_NE(original, scene_asset_fingerprint(scene));
    scene.nodes[0].vector_field_source->components_per_channel = 3;

    scene.nodes[0].terrain_render_style->path =
        SceneTerrainRenderPath::DebugWireframe;
    EXPECT_NE(original, scene_asset_fingerprint(scene));
    scene.nodes[0].terrain_render_style->path =
        SceneTerrainRenderPath::Surface;

    scene.nodes[0].terrain_mesh_source->include_backfaces = false;
    EXPECT_NE(original, scene_asset_fingerprint(scene));
    scene.nodes[0].terrain_mesh_source->include_backfaces = true;

    scene.nodes[0].terrain_height_field_source->vertical_scale = 4.0f;
    EXPECT_NE(original, scene_asset_fingerprint(scene));
}

TEST(SceneECSBoundary, EditorAuthoringDraftsDoNotInstantiateRuntimeComponents)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "source_drafts_are_authored_only";

    wz::asset::AssetKey mesh_key{};
    mesh_key.content_hash = { 0x5151, 0x6161 };
    wz::asset::AssetKey scalar_key{};
    scalar_key.content_hash = { 0x7171, 0x8181 };
    wz::asset::AssetKey vector_key{};
    vector_key.content_hash = { 0x9191, 0xA1A1 };

    SceneNodeAsset node{};
    node.id = "drafts";
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    node.mesh_render_style = SceneMeshRenderStyleAsset{
        .depth_test = true,
    };
    node.scalar_field_source = SceneScalarFieldSourceAsset{
        .kind = SceneScalarFieldSourceKind::ProceduralCheckerboard,
        .scalar_field_asset = scalar_key,
    };
    node.vector_field_source = SceneVectorFieldSourceAsset{
        .kind = SceneVectorFieldSourceKind::RawF32,
        .vector_field_asset = vector_key,
        .path = "fields/normal.raw",
    };
    node.terrain_render_style = SceneTerrainRenderStyleAsset{
        .path = SceneTerrainRenderPath::DebugWireframe,
    };
    node.terrain_mesh_source = SceneTerrainMeshSourceAsset{
        .mode = SceneTerrainMeshSourceMode::MeshAsset,
        .mesh_asset = mesh_key,
    };
    node.terrain_height_field_source = SceneTerrainHeightFieldSourceAsset{
        .mode = SceneTerrainHeightFieldSourceMode::ScalarFieldAsset,
        .scalar_field_asset = scalar_key,
    };

    EXPECT_FALSE(has_runtime_relevant_components(node));
    EXPECT_TRUE(has_asset_authoring_recipes(node));
    scene.nodes.push_back(std::move(node));

    const auto recipe_summary =
        summarize_scene_asset_authoring_recipes(scene);
    EXPECT_EQ(recipe_summary.nodes_with_recipes, 1u);
    EXPECT_EQ(recipe_summary.total_recipes, 7u);
    EXPECT_EQ(recipe_summary.mesh_sources, 1u);
    EXPECT_EQ(recipe_summary.mesh_render_styles, 1u);
    EXPECT_EQ(recipe_summary.scalar_field_sources, 1u);
    EXPECT_EQ(recipe_summary.vector_field_sources, 1u);
    EXPECT_EQ(recipe_summary.terrain_render_styles, 1u);
    EXPECT_EQ(recipe_summary.terrain_mesh_sources, 1u);
    EXPECT_EQ(recipe_summary.terrain_height_field_sources, 1u);

    const auto authored_summary = summarize_authored_scene_components(scene);
    EXPECT_EQ(authored_summary.mesh_sources, 1u);
    EXPECT_EQ(authored_summary.mesh_render_styles, 1u);
    EXPECT_EQ(authored_summary.scalar_field_sources, 1u);
    EXPECT_EQ(authored_summary.vector_field_sources, 1u);
    EXPECT_EQ(authored_summary.terrain_render_styles, 1u);
    EXPECT_EQ(authored_summary.terrain_mesh_sources, 1u);
    EXPECT_EQ(authored_summary.terrain_height_field_sources, 1u);

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const auto runtime_summary =
        summarize_scene_instance_components(result.instance);
    EXPECT_EQ(runtime_summary.terrains, 0u);
    EXPECT_EQ(runtime_summary.terrain_mesh_sources, 0u);
    EXPECT_EQ(runtime_summary.terrain_height_field_sources, 0u);
    EXPECT_EQ(runtime_summary.renderable_descriptor_slots, 1u);
    EXPECT_TRUE(result.instance.input_receivers.empty());
    EXPECT_TRUE(result.instance.ground_boundaries.empty());
    EXPECT_TRUE(result.instance.auxiliary_visuals.empty());
}

TEST(SceneECSBoundary, FingerprintIgnoresRuntimeOwnerIdentity)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "runtime_identity_independent";

    SceneNodeAsset node{};
    node.id = "node";
    node.debug_visual = SceneDebugVisualAsset{
        .kind = SceneDebugVisualKind::Axes,
    };
    scene.nodes.push_back(std::move(node));

    const uint64_t before = scene_asset_fingerprint(scene);

    auto first = instantiate_scene(scene);
    auto second = instantiate_scene(scene);
    ASSERT_TRUE(first.ok()) << first.error_detail;
    ASSERT_TRUE(second.ok()) << second.error_detail;
    EXPECT_NE(&first.instance.storage, &second.instance.storage);

    const uint64_t after = scene_asset_fingerprint(scene);
    EXPECT_EQ(before, after);
}

TEST(SceneAssetModule, TerrainMeshSourceComponentRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_terrain_mesh_source_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "terrain/source_rock",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const char* scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "terrain_mesh_source_scene",
  "nodes": [
    {
      "id": "source_mesh",
      "mesh_source": {
        "kind": "procedural_cube"
      }
    },
    {
      "id": "terrain",
      "terrain_mesh_source": {
        "mode": "scene_node",
        "source_node": "source_mesh",
        "asset": "asset://meshes/source_rock",
        "height_policy": "highest_accepted_surface",
        "min_surface_normal_y": 0.35,
        "include_backfaces": true
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "terrain_mesh_source.scene.json", scene_json);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "terrain_mesh_source",
            .path = rel_path,
            .mesh_asset_references = {
                SceneAssetReferenceBinding{
                    .uri = "asset://meshes/source_rock",
                    .key = mesh.output,
                },
            },
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 2u);

    const auto& node = scene_data->nodes[1];
    ASSERT_TRUE(node.terrain_mesh_source.has_value());
    EXPECT_EQ(
        node.terrain_mesh_source->mode,
        SceneTerrainMeshSourceMode::SceneNode);
    EXPECT_EQ(node.terrain_mesh_source->source_node, "source_mesh");
    EXPECT_EQ(node.terrain_mesh_source->mesh_asset, mesh.output);
    EXPECT_EQ(
        node.terrain_mesh_source->height_policy,
        SceneTerrainMeshHeightPolicy::HighestAcceptedSurface);
    EXPECT_FLOAT_EQ(
        node.terrain_mesh_source->min_surface_normal_y,
        0.35f);
    EXPECT_TRUE(node.terrain_mesh_source->include_backfaces);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"terrain_mesh_source\""), std::string::npos);
    EXPECT_NE(exported.find("\"scene_node\""), std::string::npos);
    EXPECT_NE(exported.find("\"source_mesh\""), std::string::npos);
    EXPECT_NE(exported.find("\"height_policy\""), std::string::npos);
    EXPECT_NE(
        exported.find("\"highest_accepted_surface\""),
        std::string::npos);
    EXPECT_NE(exported.find("\"min_surface_normal_y\""), std::string::npos);
    EXPECT_NE(exported.find("\"include_backfaces\""), std::string::npos);
    EXPECT_NE(exported.find("asset-key:"), std::string::npos);

    const wz::fs::Path reparse_root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_terrain_mesh_source_reparse_test");
    ASSERT_EQ(
        wz::fs::create_directories(reparse_root),
        wz::fs::FileError::None);

    wz::Logger reparse_logger;
    wz::gpu::Device reparse_device{};
    wz::engine::assets::EngineAssetLibrary reparse_assets{
        reparse_device, reparse_logger, reparse_root };

    auto exported_rel_path = write_scene_json(
        reparse_root,
        "terrain_mesh_source_exported.scene.json",
        exported);
    const auto exported_scene_asset =
        reparse_assets.scenes().create_scene_from_json({
            .name = "terrain_mesh_source_exported",
            .path = exported_rel_path,
        });
    ASSERT_TRUE(exported_scene_asset.valid());
    ASSERT_TRUE(reparse_assets.commit());
    ASSERT_TRUE(reparse_assets.resolve_all().ok());

    const auto* reparsed_scene_data = reparse_assets.scenes().get_scene_data(
        reparse_assets.scenes().get_scene(exported_scene_asset));
    ASSERT_NE(reparsed_scene_data, nullptr);
    ASSERT_EQ(reparsed_scene_data->nodes.size(), 2u);
    ASSERT_TRUE(
        reparsed_scene_data->nodes[1].terrain_mesh_source.has_value());
    EXPECT_EQ(
        reparsed_scene_data->nodes[1].terrain_mesh_source->source_node,
        "source_mesh");

    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    const auto runtime_summary =
        summarize_scene_instance_components(result.instance);
    EXPECT_EQ(runtime_summary.terrain_mesh_sources, 0u);
}

TEST(SceneAssetModule, SceneImportSourceRoundTripsThroughSceneJSON)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_import_source_roundtrip_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    SceneAssetData authored{};
    authored.name = "scene_import_source_roundtrip";

    SceneNodeAsset anchor = make_scene_node("tank_anchor");
    anchor.scene_import_source = SceneImportSourceAsset{
        .kind = SceneImportSourceKind::GLB,
        .path = "gltf/tank1.glb",
        .import_prefix = "tank_anchor/tank1",
        .scene_index = 0u,
    };
    authored.nodes.push_back(std::move(anchor));

    SceneNodeAsset turret = make_scene_node(
        "tank_anchor/tank1/turret",
        "turret");
    turret.parent_id = "tank_anchor/tank1/body";
    turret.imported_node = SceneImportedNodeAsset{
        .anchor_node = "tank_anchor",
        .import_prefix = "tank_anchor/tank1",
        .source_node_id = "turret",
        .missing_source = false,
    };
    turret.behavior = SceneBehaviorAsset{
        .module = "tank",
        .name = "rotate_turret",
    };
    authored.nodes.push_back(std::move(turret));

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(authored));
    EXPECT_NE(exported.find("\"scene_import_source\""), std::string::npos);
    EXPECT_NE(exported.find("\"imported_node\""), std::string::npos);
    EXPECT_NE(exported.find("\"tank_anchor/tank1\""), std::string::npos);

    auto rel_path = write_scene_json(
        root,
        "scene_import_source.scene.json",
        exported);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "scene_import_source",
        .path = rel_path,
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(data, nullptr);
    ASSERT_EQ(data->nodes.size(), 2u);

    const auto* parsed_anchor = find_scene_node(*data, "tank_anchor");
    ASSERT_NE(parsed_anchor, nullptr);
    ASSERT_TRUE(parsed_anchor->scene_import_source.has_value());
    EXPECT_EQ(parsed_anchor->scene_import_source->kind,
        SceneImportSourceKind::GLB);
    EXPECT_EQ(parsed_anchor->scene_import_source->path, "gltf/tank1.glb");
    EXPECT_EQ(
        parsed_anchor->scene_import_source->import_prefix,
        "tank_anchor/tank1");
    ASSERT_TRUE(parsed_anchor->scene_import_source->scene_index.has_value());
    EXPECT_EQ(*parsed_anchor->scene_import_source->scene_index, 0u);

    const auto* parsed_turret =
        find_scene_node(*data, "tank_anchor/tank1/turret");
    ASSERT_NE(parsed_turret, nullptr);
    ASSERT_TRUE(parsed_turret->imported_node.has_value());
    EXPECT_EQ(parsed_turret->imported_node->anchor_node, "tank_anchor");
    EXPECT_EQ(parsed_turret->imported_node->source_node_id, "turret");
    EXPECT_FALSE(parsed_turret->imported_node->missing_source);
    ASSERT_TRUE(parsed_turret->behavior.has_value());
    EXPECT_EQ(parsed_turret->behavior->module, "tank");
}

TEST(SceneAssetModule, ParsesExportsAndSummarizesScalarFieldSource)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_scalar_field_source_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const std::string scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "scalar_field_source_scene",
  "nodes": [
    {
      "id": "root",
      "transform": {
        "translation": [0, 0, 0]
      },
      "scalar_field_source": {
        "kind": "procedural_checkerboard",
        "width": 32,
        "height": 16,
        "depth": 1,
        "frequency": 4.0,
        "amplitude": 2.0
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "scalar_field_source.scene.json", scene_json);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "scalar_field_source",
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
    ASSERT_TRUE(node.scalar_field_source.has_value());
    EXPECT_EQ(
        node.scalar_field_source->kind,
        SceneScalarFieldSourceKind::ProceduralCheckerboard);
    EXPECT_EQ(node.scalar_field_source->width, 32u);
    EXPECT_EQ(node.scalar_field_source->height, 16u);
    EXPECT_EQ(node.scalar_field_source->depth, 1u);
    EXPECT_FLOAT_EQ(node.scalar_field_source->frequency, 4.0f);
    EXPECT_FLOAT_EQ(node.scalar_field_source->amplitude, 2.0f);

    const auto summary = summarize_authored_scene_components(*scene_data);
    EXPECT_EQ(summary.scalar_field_sources, 1u);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"scalar_field_source\""), std::string::npos);
    EXPECT_NE(exported.find("\"procedural_checkerboard\""), std::string::npos);
    EXPECT_NE(exported.find("\"frequency\""), std::string::npos);
    EXPECT_NE(exported.find("\"amplitude\""), std::string::npos);
}

TEST(SceneAssetModule, ParsesExportsAndSummarizesVectorFieldSource)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_vector_field_source_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const std::string scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "vector_field_source_scene",
  "nodes": [
    {
      "id": "normal",
      "transform": {
        "translation": [0, 0, 0]
      },
      "vector_field_source": {
        "kind": "raw_f32",
        "path": "normals.raw",
        "width": 32,
        "height": 16,
        "depth": 1,
        "components_per_channel": 3,
        "channels": ["normal"]
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "vector_field_source.scene.json", scene_json);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "vector_field_source",
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
    ASSERT_TRUE(node.vector_field_source.has_value());
    EXPECT_EQ(
        node.vector_field_source->kind,
        SceneVectorFieldSourceKind::RawF32);
    EXPECT_EQ(node.vector_field_source->path, "normals.raw");
    EXPECT_EQ(node.vector_field_source->width, 32u);
    EXPECT_EQ(node.vector_field_source->height, 16u);
    EXPECT_EQ(node.vector_field_source->depth, 1u);
    EXPECT_EQ(node.vector_field_source->components_per_channel, 3u);
    ASSERT_EQ(node.vector_field_source->channels.size(), 1u);
    EXPECT_EQ(node.vector_field_source->channels[0].name, "normal");

    const auto recipe_summary =
        summarize_scene_asset_authoring_recipes(*scene_data);
    EXPECT_EQ(recipe_summary.vector_field_sources, 1u);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"vector_field_source\""), std::string::npos);
    EXPECT_NE(exported.find("\"raw_f32\""), std::string::npos);
    EXPECT_NE(
        exported.find("\"components_per_channel\""),
        std::string::npos);
    EXPECT_NE(exported.find("\"normal\""), std::string::npos);
}

TEST(SceneAssetModule, TerrainHeightFieldSourceComponentRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_terrain_height_field_source_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const auto field = assets.scalar_fields().create_procedural_scalar_field({
        .name = "terrain/height_field",
        .width = 8,
        .height = 8,
        .depth = 1,
        .generator = ScalarFieldGenerator::GradientY,
    });
    ASSERT_TRUE(field.valid());

    const char* scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "terrain_height_field_source_scene",
  "nodes": [
    {
      "id": "height",
      "scalar_field_source": {
        "kind": "procedural_gradient_y",
        "width": 8,
        "height": 8,
        "depth": 1
      }
    },
    {
      "id": "terrain",
      "terrain_height_field_source": {
        "mode": "scene_node",
        "source_node": "height",
        "asset": "asset://scalar_fields/height",
        "origin": [-2.0, -3.0],
        "size": [10.0, 12.0],
        "vertical_scale": 4.0,
        "base_height": -1.0
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "terrain_height_field_source.scene.json", scene_json);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "terrain_height_field_source",
            .path = rel_path,
            .scalar_field_asset_references = {
                SceneAssetReferenceBinding{
                    .uri = "asset://scalar_fields/height",
                    .key = field.output,
                },
            },
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 2u);

    const auto& node = scene_data->nodes[1];
    ASSERT_TRUE(node.terrain_height_field_source.has_value());
    EXPECT_EQ(
        node.terrain_height_field_source->mode,
        SceneTerrainHeightFieldSourceMode::SceneNode);
    EXPECT_EQ(node.terrain_height_field_source->source_node, "height");
    EXPECT_EQ(node.terrain_height_field_source->scalar_field_asset,
        field.output);
    EXPECT_FLOAT_EQ(node.terrain_height_field_source->origin[0], -2.0f);
    EXPECT_FLOAT_EQ(node.terrain_height_field_source->origin[1], -3.0f);
    EXPECT_FLOAT_EQ(node.terrain_height_field_source->size[0], 10.0f);
    EXPECT_FLOAT_EQ(node.terrain_height_field_source->size[1], 12.0f);
    EXPECT_FLOAT_EQ(
        node.terrain_height_field_source->vertical_scale,
        4.0f);
    EXPECT_FLOAT_EQ(node.terrain_height_field_source->base_height, -1.0f);

    const auto summary = summarize_authored_scene_components(*scene_data);
    EXPECT_EQ(summary.scalar_field_sources, 1u);
    EXPECT_EQ(summary.terrain_height_field_sources, 1u);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(
        exported.find("\"terrain_height_field_source\""),
        std::string::npos);
    EXPECT_NE(exported.find("\"scene_node\""), std::string::npos);
    EXPECT_NE(exported.find("\"vertical_scale\""), std::string::npos);
    EXPECT_NE(exported.find("\"base_height\""), std::string::npos);
    EXPECT_NE(exported.find("asset-key:"), std::string::npos);
}

TEST(SceneAssetModule, LightComponentsRoundTripThroughSceneJSON)
{
    using namespace wz::engine::assets;

    wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wozzits_scene_light_component_roundtrip_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData authored{};
    authored.name = "light_component_scene";
    SceneNodeAsset node{};
    node.id = "sun";
    node.direct_light_source = SceneDirectLightSourceAsset{
        .kind = DirectLightKind::Spot,
        .color = { 1.0f, 0.8f, 0.6f },
        .intensity = 4.0f,
        .range = 32.0f,
        .inner_cone_radians = 0.25f,
        .outer_cone_radians = 0.75f,
    };
    node.ambient_lighting = SceneAmbientLightingAsset{
        .mode = AmbientLightingMode::FieldModulated,
        .color = { 0.2f, 0.3f, 0.5f },
        .intensity = 0.45f,
        .domain_mapping = AmbientLightingDomainMapping::WorldXZ,
    };
    node.hdri_environment = SceneHDRIEnvironmentAsset{
        .path = "studio.hdr",
        .format = HDRIEnvironmentFormat::RadianceHDR,
        .exposure = 0.5f,
        .rotation_x_radians = 0.125f,
        .rotation_y_radians = 1.25f,
        .rotation_z_radians = -0.25f,
        .lighting_intensity = 0.75f,
        .reflection_intensity = 0.6f,
        .background_intensity = 0.0f,
        .lighting_sample_resolution = 512,
        .dominant_light_direction = { 0.0f, -0.5f, 0.8660254f },
        .dominant_light_color = { 1.0f, 0.92f, 0.82f },
        .dominant_light_intensity = 3.0f,
        .dominant_light_confidence = 0.8f,
    };
    node.sky_visual = SceneSkyVisualAsset{
        .kind = SceneSkyVisualKind::VectorField,
        .gradient_top_color = { 0.1f, 0.2f, 0.3f },
        .gradient_bottom_color = { 0.7f, 0.8f, 0.9f },
        .texture_asset = wz::asset::AssetKey{
            .content_hash = { 1, 2 },
            .schema_hash = { 3, 4 },
            .compiler_hash = { 5, 6 },
            .deps_hash = { 7, 8 },
        },
        .texture_path = "skies/studio.exr",
        .texture_format = HDRIEnvironmentFormat::OpenEXR,
        .vector_field_asset = wz::asset::AssetKey{
            .content_hash = { 11, 12 },
            .schema_hash = { 13, 14 },
            .compiler_hash = { 15, 16 },
            .deps_hash = { 17, 18 },
        },
        .vector_field_node = "wind_field",
        .exposure = -0.5f,
        .rotation_y_radians = 0.25f,
    };
    node.sky_surface = SceneSkySurfaceAsset{
        .projection = SceneSkyProjection::Sphere,
        .radius = 42.0f,
        .visible_to_camera = true,
    };
    authored.nodes.push_back(std::move(node));

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(authored));
    EXPECT_NE(exported.find("\"direct_light_source\""), std::string::npos);
    EXPECT_NE(exported.find("\"ambient_lighting\""), std::string::npos);
    EXPECT_NE(exported.find("\"hdri_environment\""), std::string::npos);
    EXPECT_NE(exported.find("\"sky_visual\""), std::string::npos);
    EXPECT_NE(exported.find("\"sky_surface\""), std::string::npos);
    EXPECT_NE(exported.find("\"gradient_top_color\""), std::string::npos);
    EXPECT_NE(exported.find("\"field_modulated\""), std::string::npos);
    EXPECT_NE(exported.find("\"world_xz\""), std::string::npos);
    EXPECT_NE(exported.find("\"radiance_hdr\""), std::string::npos);

    auto rel_path = write_scene_json(
        root, "light_component.scene.json", exported);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "light_component",
            .path = rel_path,
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);
    ASSERT_TRUE(scene_data->nodes[0].direct_light_source.has_value());
    ASSERT_TRUE(scene_data->nodes[0].ambient_lighting.has_value());
    ASSERT_TRUE(scene_data->nodes[0].hdri_environment.has_value());
    ASSERT_TRUE(scene_data->nodes[0].sky_visual.has_value());
    ASSERT_TRUE(scene_data->nodes[0].sky_surface.has_value());
    EXPECT_EQ(
        scene_data->nodes[0].direct_light_source->kind,
        DirectLightKind::Spot);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].direct_light_source->outer_cone_radians,
        0.75f);
    EXPECT_EQ(
        scene_data->nodes[0].ambient_lighting->mode,
        AmbientLightingMode::FieldModulated);
    EXPECT_EQ(
        scene_data->nodes[0].ambient_lighting->domain_mapping,
        AmbientLightingDomainMapping::WorldXZ);
    EXPECT_EQ(
        scene_data->nodes[0].hdri_environment->format,
        HDRIEnvironmentFormat::RadianceHDR);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].hdri_environment->rotation_x_radians,
        0.125f);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].hdri_environment->rotation_y_radians,
        1.25f);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].hdri_environment->rotation_z_radians,
        -0.25f);
    EXPECT_EQ(
        scene_data->nodes[0].hdri_environment->lighting_sample_resolution,
        512u);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].hdri_environment->dominant_light_confidence,
        0.8f);
    EXPECT_EQ(
        scene_data->nodes[0].sky_visual->kind,
        SceneSkyVisualKind::VectorField);
    EXPECT_FALSE(
        scene_data->nodes[0].sky_visual->texture_asset
        == wz::asset::AssetKey{});
    EXPECT_EQ(
        scene_data->nodes[0].sky_visual->texture_path,
        "skies/studio.exr");
    EXPECT_EQ(
        scene_data->nodes[0].sky_visual->texture_format,
        HDRIEnvironmentFormat::OpenEXR);
    EXPECT_FALSE(
        scene_data->nodes[0].sky_visual->vector_field_asset
        == wz::asset::AssetKey{});
    EXPECT_EQ(
        scene_data->nodes[0].sky_visual->vector_field_node,
        "wind_field");
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].sky_visual->rotation_y_radians,
        0.25f);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].sky_visual->gradient_top_color[1],
        0.2f);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].sky_visual->gradient_bottom_color[2],
        0.9f);
    EXPECT_EQ(
        scene_data->nodes[0].sky_surface->projection,
        SceneSkyProjection::Sphere);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].sky_surface->radius, 42.0f);

    const auto components = authored_components_for_node(scene_data->nodes[0]);
    EXPECT_EQ(std::count(
        components.begin(),
        components.end(),
        wz::scene::SceneAuthoredComponentKind::Light), 1);
    EXPECT_EQ(std::count(
        components.begin(),
        components.end(),
        wz::scene::SceneAuthoredComponentKind::AmbientLighting), 1);
    EXPECT_EQ(std::count(
        components.begin(),
        components.end(),
        wz::scene::SceneAuthoredComponentKind::HDRIEnvironment), 1);

    const auto summary = summarize_authored_scene_components(*scene_data);
    EXPECT_EQ(summary.hdri_environments, 1u);
}
