#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif

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














// ─── Issue #56: non-render component descriptors ────────────────────────













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












// ─── Issue #57: debug visual descriptor tests ───────────────────────────





// ─── Issue #57: debug visual validation tests ───────────────────────────







// Issue #61: editor handle validation tests. Missing kind is rejected so
// authored tool intent stays explicit.






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

    // Issue #195: the legacy 0x700 mesh-wireframe renderable — the old
    // universal "some renderable asset" test stand-in — was deleted (the 0x706
    // RHI pull mesh replaced it, but it requires a render program whose
    // shaders cannot compile deviceless). Tests that need a REGISTERED,
    // deviceless-RESOLVABLE, MESH-KIND renderable asset (instantiate_scene
    // without a resource resolver only placeholders mesh-kind renderables) use
    // the terrain debug renderable (0x703) — the surviving deviceless producer
    // of Mesh-kind RenderableAssetData. It rides the #222 TerrainAsset
    // deprecation; when it goes, these stand-ins move to the rhi path (#179).
    inline wz::engine::assets::RenderableAsset create_test_preview_renderable(
        wz::engine::assets::EngineAssetLibrary& assets,
        const std::string& name)
    {
        // A MESH-representation terrain: its debug renderable compiles to
        // RenderableKind::Mesh (a heightfield terrain would compile to
        // ScalarField-kind, which instantiate_scene only accepts with a
        // resource resolver).
        const auto mesh = assets.meshes().create_procedural_mesh({
            .name = name + "_standin_mesh",
            .kind = wz::engine::assets::ProceduralMeshKind::Cube,
        });
        if (!mesh.valid()) {
            return {};
        }
        const auto terrain = assets.terrains().create_from_mesh({
            .name = name + "_terrain",
            .mesh = mesh,
        });
        if (!terrain.valid()) {
            return {};
        }
        return assets.renderables().create_terrain_debug({
            .name = name,
            .terrain = terrain,
        });
    }

    // Issue #195: serves directly-constructed RenderableAssetData for
    // fabricated keys, so the legacy resolver pipeline (instantiate_scene +
    // MeshSceneRenderResourceResolver, which realizes RenderableAssetData)
    // keeps its coverage without any legacy renderable SCHEMA producing that
    // data. The legacy resolver machinery itself survives until #179; when it
    // goes, the tests riding this resolver go with it.
    class DirectRenderableResolver final
        : public wz::engine::assets::SceneRenderableResolver
    {
    public:
        void add(
            const wz::asset::AssetKey& key,
            wz::engine::assets::RenderableAssetData data)
        {
            entries_[key] = std::move(data);
        }

        const wz::engine::assets::RenderableAssetData* get(
            wz::asset::AssetKey key) const override
        {
            const auto it = entries_.find(key);
            return it != entries_.end() ? &it->second : nullptr;
        }

    private:
        std::unordered_map<
            wz::asset::AssetKey,
            wz::engine::assets::RenderableAssetData,
            wz::asset::AssetKeyHash> entries_;
    };

    // A distinct, stable fabricated key for DirectRenderableResolver entries.
    inline wz::asset::AssetKey direct_renderable_key(uint64_t id)
    {
        return wz::asset::AssetKey{
            .content_hash = { id, 0x195195195ull },
            .schema_hash = { id ^ 0x1000ull, 0x195195196ull },
            .compiler_hash = { id ^ 0x2000ull, 0x195195197ull },
            .deps_hash = { id ^ 0x3000ull, 0x195195198ull },
        };
    }
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





// ─── Issue #59: concrete mesh render resource resolver ────────────────







































#if defined(__clang__)
#pragma clang diagnostic pop
#endif
