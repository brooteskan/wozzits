#include <gtest/gtest.h>

#include <asset/draft.h>
#include <engine/abi/wozzits_abi.h>
#include <engine/assets/gltf/gltf_importer.h>
#include <engine/editor/asset_graph_layout.h>
#include <engine/editor/asset_graph_snapshot.h>
#include <engine/editor/project_snapshot.h>
#include <engine/editor/project_snapshot_abi.h>
#include <engine/project/project_manifest.h>
#include <engine/project/project_runtime_launch.h>
#include <engine/editor/scene_snapshot.h>

#include <file/filesystem.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    struct TempProjectRoot
    {
        fs::path root;

        TempProjectRoot()
        {
            const auto suffix = std::to_string(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count());
            root = fs::temp_directory_path()
                / ("wozzits_project_manifest_tests_" + suffix);
            std::error_code ec;
            fs::remove_all(root, ec);
        }

        ~TempProjectRoot()
        {
            std::error_code ec;
            fs::remove_all(root, ec);
        }
    };

    void write_text_file(const fs::path& path, const std::string& text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        ASSERT_TRUE(file.good()) << "failed to open " << path.string();
        file << text;
    }

    fs::path manifest_path(const fs::path& project_root)
    {
        return project_root / ".wozzits" / "project.json";
    }

    std::string abi_string(
        const std::vector<uint8_t>& blob,
        WzEditorStringSpan span)
    {
        EXPECT_LE(span.offset + span.size, blob.size());
        return std::string(
            reinterpret_cast<const char*>(blob.data() + span.offset),
            static_cast<size_t>(span.size));
    }

    template <typename T>
    const T* abi_table(
        const std::vector<uint8_t>& blob,
        WzEditorTableSpan span)
    {
        EXPECT_EQ(span.offset % alignof(T), 0u);
        EXPECT_LE(span.offset + sizeof(T) * span.count, blob.size());
        return reinterpret_cast<const T*>(blob.data() + span.offset);
    }
}

TEST(ProjectManifest, LoadsRelativeProjectRootFromResourceRoot)
{
    TempProjectRoot temp;
    const fs::path project_root = temp.root / "projects" / "sample";
    const std::string absolute_module =
        (temp.root / "external" / "module").generic_string();

    write_text_file(
        manifest_path(project_root),
        std::string{
            R"json({
  "schema": "wozzits.project.v1",
  "formatVersion": 1,
  "name": "Sample",
  "rhi_render_path": true,
  "scene": "scenes/main.scene.json",
  "asset_graph": "assets.graph.json",
  "behavior_project_folder": "behavior",
  "behavior_module_folder": ")json" }
            + absolute_module
            + R"json("
})json");

    const auto loaded = wz::engine::project::load_project_manifest(
        wz::engine::project::ProjectManifestLoadDesc{
            .project_root = "projects/sample",
            .resource_root = temp.root.string(),
        });

    ASSERT_TRUE(loaded.ok) << loaded.error;
    EXPECT_EQ(loaded.manifest.root, "projects/sample");
    EXPECT_EQ(
        loaded.manifest.manifest_path,
        wz::engine::project::project_manifest_path("projects/sample"));
    EXPECT_EQ(loaded.manifest.schema, "wozzits.project.v1");
    EXPECT_EQ(loaded.manifest.format_version, 1u);
    EXPECT_EQ(loaded.manifest.name, "Sample");
    EXPECT_TRUE(loaded.manifest.rhi_render_path);
    EXPECT_EQ(
        loaded.manifest.scene_path,
        wz::fs::join("projects/sample", "scenes/main.scene.json"));
    EXPECT_EQ(
        loaded.manifest.asset_graph_path,
        wz::fs::join("projects/sample", "assets.graph.json"));
    EXPECT_EQ(
        loaded.manifest.behavior_project_folder,
        wz::fs::join("projects/sample", "behavior"));
    EXPECT_EQ(loaded.manifest.behavior_module_folder, absolute_module);
}

TEST(ProjectManifest, MinimalManifestAllowsEmptyEditorProject)
{
    TempProjectRoot temp;
    const fs::path project_root = temp.root / "empty_project";

    write_text_file(
        manifest_path(project_root),
        R"json({
  "schema": "wozzits.project.v1",
  "formatVersion": 1,
  "name": "Empty"
})json");

    const auto loaded = wz::engine::project::load_project_manifest(
        wz::engine::project::ProjectManifestLoadDesc{
            .project_root = project_root.string(),
        });

    ASSERT_TRUE(loaded.ok) << loaded.error;
    EXPECT_EQ(loaded.manifest.root, project_root.string());
    EXPECT_EQ(loaded.manifest.name, "Empty");
    EXPECT_FALSE(loaded.manifest.rhi_render_path);
    EXPECT_TRUE(loaded.manifest.scene_path.empty());
    EXPECT_TRUE(loaded.manifest.asset_graph_path.empty());
    EXPECT_TRUE(loaded.manifest.behavior_project_folder.empty());
    EXPECT_TRUE(loaded.manifest.behavior_module_folder.empty());
}

TEST(ProjectManifest, ProbeDistinguishesMissingInvalidAndValid)
{
    TempProjectRoot temp;
    const fs::path project_root = temp.root / "probe_project";

    const auto missing = wz::engine::project::probe_project_manifest(
        wz::engine::project::ProjectManifestLoadDesc{
            .project_root = project_root.string(),
        });
    EXPECT_EQ(
        missing.status,
        wz::engine::project::ProjectManifestProbeStatus::Missing);

    write_text_file(
        manifest_path(project_root),
        R"json({
  "formatVersion": 1
})json");

    const auto invalid = wz::engine::project::probe_project_manifest(
        wz::engine::project::ProjectManifestLoadDesc{
            .project_root = project_root.string(),
        });
    EXPECT_EQ(
        invalid.status,
        wz::engine::project::ProjectManifestProbeStatus::Invalid);
    EXPECT_FALSE(invalid.error.empty());

    write_text_file(
        manifest_path(project_root),
        R"json({
  "schema": "wozzits.project.v1",
  "formatVersion": 1,
  "name": "Probe"
})json");

    const auto valid = wz::engine::project::probe_project_manifest(
        wz::engine::project::ProjectManifestLoadDesc{
            .project_root = project_root.string(),
        });
    EXPECT_EQ(
        valid.status,
        wz::engine::project::ProjectManifestProbeStatus::Valid);
    EXPECT_EQ(valid.manifest.name, "Probe");
}

TEST(ProjectManifest, CreateWritesEngineDefinedDefaultManifest)
{
    TempProjectRoot temp;
    const fs::path project_root = temp.root / "created_project";

    const auto created = wz::engine::project::create_project_manifest(
        wz::engine::project::ProjectManifestCreateDesc{
            .project_root = project_root.string(),
            .name = "Created",
        });

    ASSERT_TRUE(created.ok) << created.error;
    EXPECT_TRUE(created.created);
    EXPECT_EQ(created.manifest.schema, "wozzits.project.v1");
    EXPECT_EQ(created.manifest.format_version, 1u);
    EXPECT_EQ(created.manifest.name, "Created");
    EXPECT_TRUE(fs::exists(manifest_path(project_root)));

    const auto probed = wz::engine::project::probe_project_manifest(
        wz::engine::project::ProjectManifestLoadDesc{
            .project_root = project_root.string(),
        });
    EXPECT_EQ(
        probed.status,
        wz::engine::project::ProjectManifestProbeStatus::Valid);
    EXPECT_EQ(probed.manifest.name, "Created");
}

TEST(ProjectRuntimeLaunch, LoadsExplicitRuntimePaths)
{
    TempProjectRoot temp;
    const fs::path project_root = temp.root / "projects" / "runtime";

    write_text_file(
        manifest_path(project_root),
        R"json({
  "schema": "wozzits.project.v1",
  "formatVersion": 1,
  "name": "Runtime",
  "rhi_render_path": true,
  "scene": "scene.json",
  "asset_graph": "assets.graph.json"
})json");

    const auto loaded = wz::engine::project::load_project_runtime_launch(
        wz::engine::project::ProjectRuntimeLaunchDesc{
            .project_root = "projects/runtime",
            .resource_root = temp.root.string(),
        });

    const wz::fs::Path expected_root =
        wz::fs::normalize(project_root.string());

    ASSERT_TRUE(loaded.ok) << loaded.error;
    EXPECT_EQ(loaded.launch.manifest.name, "Runtime");
    EXPECT_EQ(loaded.launch.manifest.root, expected_root);
    EXPECT_EQ(loaded.launch.resource_root, expected_root);
    EXPECT_EQ(
        loaded.launch.asset_graph_path,
        wz::fs::join(expected_root, "assets.graph.json"));
    EXPECT_EQ(
        loaded.launch.scene_path,
        wz::fs::join(expected_root, "scene.json"));
}

TEST(ProjectRuntimeLaunch, RejectsEditorOnlyManifest)
{
    TempProjectRoot temp;
    const fs::path project_root = temp.root / "editor_only";

    write_text_file(
        manifest_path(project_root),
        R"json({
  "schema": "wozzits.project.v1",
  "formatVersion": 1,
  "name": "Editor Only"
})json");

    const auto loaded = wz::engine::project::load_project_runtime_launch(
        wz::engine::project::ProjectRuntimeLaunchDesc{
            .project_root = project_root.string(),
        });

    EXPECT_FALSE(loaded.ok);
    EXPECT_NE(
        loaded.error.find("project does not enable the RHI render path"),
        std::string::npos);
}

TEST(ProjectSceneSnapshot, LoadsSceneTreeFromManifest)
{
    TempProjectRoot temp;
    const fs::path project_root = temp.root / "scene_snapshot_project";

    write_text_file(
        manifest_path(project_root),
        R"json({
  "schema": "wozzits.project.v1",
  "formatVersion": 1,
  "name": "Scene Snapshot",
  "scene": "scene.json"
})json");

    write_text_file(
        project_root / "scene.json",
        R"json({
  "schema": "wozzits.scene.v0",
  "name": "main_scene",
  "nodes": [
    {
      "id": "root",
      "parent": null,
      "visible": true,
      "transform": {
        "translation": [0, 0, 0],
        "rotation_quat": [0, 0, 0, 1],
        "scale": [1, 1, 1]
      }
    },
    {
      "id": "camera",
      "parent": "root",
      "camera": {
        "fov_y": 1.0472,
        "near": 0.1,
        "far": 1000,
        "aspect": 1.7778
      }
    },
    {
      "id": "mesh",
      "parent": "root",
      "name": "mesh node",
      "renderable": {
        "asset_graph_node_id": 8
      }
    }
  ]
})json");

    const auto loaded = wz::engine::editor::load_project_scene_snapshot(
        wz::engine::project::ProjectManifestLoadDesc{
            .project_root = project_root.string(),
        });

    ASSERT_TRUE(loaded.ok) << loaded.error;
    EXPECT_EQ(loaded.snapshot.schema, "wozzits.scene.v0");
    EXPECT_EQ(loaded.snapshot.name, "main_scene");

    ASSERT_EQ(loaded.snapshot.roots.size(), 1u);
    const auto& root = loaded.snapshot.roots[0];
    EXPECT_EQ(root.id, "root");
    EXPECT_EQ(root.display_name, "root");
    ASSERT_TRUE(root.visible);
    EXPECT_TRUE(*root.visible);
    EXPECT_EQ(root.renderable_source.kind, "none");
    EXPECT_EQ(root.renderable_source.display_name, "none");
    ASSERT_TRUE(root.transform);
    ASSERT_EQ(root.transform->rotation_quat.size(), 4u);
    EXPECT_DOUBLE_EQ(root.transform->rotation_quat[3], 1.0);
    ASSERT_EQ(root.transform->rotation_euler_degrees.size(), 3u);
    EXPECT_DOUBLE_EQ(root.transform->rotation_euler_degrees[0], 0.0);
    EXPECT_DOUBLE_EQ(root.transform->rotation_euler_degrees[1], 0.0);
    EXPECT_DOUBLE_EQ(root.transform->rotation_euler_degrees[2], 0.0);
    EXPECT_EQ(root.transform->display.translation_x, "0");
    EXPECT_EQ(root.transform->display.rotation_x, "0");
    EXPECT_EQ(root.transform->display.scale_x, "1");
    ASSERT_EQ(root.children.size(), 2u);

    const auto& camera = root.children[0];
    EXPECT_EQ(camera.id, "camera");
    EXPECT_EQ(camera.parent_id, std::optional<std::string>("root"));
    EXPECT_EQ(camera.kind, "camera");
    ASSERT_EQ(camera.components.size(), 1u);
    EXPECT_EQ(camera.components[0].kind, "camera");
    EXPECT_EQ(camera.components[0].display_name, "Camera");
    ASSERT_TRUE(camera.camera);
    ASSERT_TRUE(camera.camera->fov_y);
    EXPECT_DOUBLE_EQ(*camera.camera->fov_y, 1.0472);

    const auto& mesh = root.children[1];
    EXPECT_EQ(mesh.id, "mesh");
    EXPECT_EQ(mesh.display_name, "mesh node");
    EXPECT_EQ(mesh.kind, "renderable");
    EXPECT_EQ(mesh.renderable_source.kind, "asset-graph-node");
    EXPECT_EQ(mesh.renderable_source.display_name, "asset-node");
    ASSERT_TRUE(mesh.renderable);
    ASSERT_TRUE(mesh.renderable->asset_graph_node_id);
    EXPECT_EQ(*mesh.renderable->asset_graph_node_id, 8u);
    EXPECT_EQ(mesh.renderable->source.kind, "asset-graph-node");
    EXPECT_EQ(mesh.renderable->source.display_name, "asset-node");
}

// issue #213: build_scene_snapshot_from_nodes maps live SceneNodeAssets (the
// runtime-grafted instance sub-tree) into a SceneSnapshot the editor merges
// under hosts. The grafted nodes mirror what graft_scene_sources produces: a
// host-namespaced body (parent = the host, which is NOT in the grafted set) plus
// nested turret/gun. The host id is absent from `nodes`, so body becomes a root
// but KEEPS parent_id = the host so the editor can find where to graft.
TEST(ProjectSceneSnapshot, BuildsSnapshotFromGraftedNodes)
{
    std::vector<wz::engine::assets::SceneNodeAsset> nodes;

    wz::engine::assets::SceneNodeAsset body;
    body.id = "host/body";
    body.parent_id = "host";  // host is NOT in the set => body is a root
    body.name = "body";
    body.local.translation[0] = 1.0f;
    body.local.translation[1] = 2.0f;
    body.local.translation[2] = 3.0f;
    // A grafted GLB child resolves a mesh-styled renderable (no asset-graph id).
    body.renderable_asset = wz::asset::AssetKey{};
    nodes.push_back(body);

    wz::engine::assets::SceneNodeAsset turret;
    turret.id = "host/body/turret";
    turret.parent_id = "host/body";
    turret.name = "turret";
    nodes.push_back(turret);

    wz::engine::assets::SceneNodeAsset gun;
    gun.id = "host/body/turret/gun";
    gun.parent_id = "host/body/turret";
    gun.name = "gun";
    gun.visible = false;
    nodes.push_back(gun);

    const wz::engine::editor::SceneSnapshot snapshot =
        wz::engine::editor::build_scene_snapshot_from_nodes(nodes);

    EXPECT_EQ(snapshot.schema, "wozzits.scene.v0");

    // body is the sole root (its parent "host" is outside the set) and keeps its
    // parent_id so the editor can graft the sub-tree under the host.
    ASSERT_EQ(snapshot.roots.size(), 1u);
    const auto& root = snapshot.roots[0];
    EXPECT_EQ(root.id, "host/body");
    EXPECT_EQ(root.display_name, "body");
    EXPECT_EQ(root.parent_id, std::optional<std::string>("host"));
    ASSERT_TRUE(root.visible);
    EXPECT_TRUE(*root.visible);
    // Renderable presence surfaces as a renderable node, source "scene-source".
    EXPECT_EQ(root.kind, "renderable");
    EXPECT_EQ(root.renderable_source.kind, "scene-source");
    ASSERT_TRUE(root.transform);
    EXPECT_EQ(root.transform->display.translation_x, "1");
    EXPECT_EQ(root.transform->display.translation_y, "2");
    EXPECT_EQ(root.transform->display.translation_z, "3");

    // turret nests under body, gun nests under turret (parent_id chain).
    ASSERT_EQ(root.children.size(), 1u);
    const auto& turret_node = root.children[0];
    EXPECT_EQ(turret_node.id, "host/body/turret");
    EXPECT_EQ(turret_node.parent_id,
        std::optional<std::string>("host/body"));
    EXPECT_EQ(turret_node.kind, "node");  // no renderable => plain node

    ASSERT_EQ(turret_node.children.size(), 1u);
    const auto& gun_node = turret_node.children[0];
    EXPECT_EQ(gun_node.id, "host/body/turret/gun");
    EXPECT_EQ(gun_node.parent_id,
        std::optional<std::string>("host/body/turret"));
    ASSERT_TRUE(gun_node.visible);
    EXPECT_FALSE(*gun_node.visible);
}

// A snapshot built from live scene nodes must surface behavior bindings, so the
// prefab-editor open_scene round-trip (which refreshes the tree from the running
// scene, not disk) does not blank out every node's bindings. Mirrors the JSON
// path's read_behaviors: single `behavior` + the `behaviors` list, config kinds.
TEST(ProjectSceneSnapshot, SurfacesBehaviorsFromNodes)
{
    wz::engine::assets::SceneNodeAsset tank;
    tank.id = "empty_1";
    tank.name = "tank";

    wz::engine::assets::SceneBehaviorAsset spawner;
    spawner.id = "empty_1.behavior.2";
    spawner.module = "prefab_spawner";
    spawner.name = "prefab_spawner";
    spawner.enabled = true;
    spawner.events = { "frame.update" };
    spawner.config.push_back({
        .key = "prefab_name",
        .kind = wz::engine::assets::SceneBehaviorConfigValueKind::String,
        .string_value = "enemy_tank",
    });
    spawner.config.push_back({
        .key = "offset_x",
        .kind = wz::engine::assets::SceneBehaviorConfigValueKind::Number,
        .number_value = 24.0,
    });
    tank.behaviors.push_back(spawner);

    const wz::engine::editor::SceneSnapshot snapshot =
        wz::engine::editor::build_scene_snapshot_from_nodes({ tank });

    ASSERT_EQ(snapshot.roots.size(), 1u);
    const auto& node = snapshot.roots[0];
    ASSERT_EQ(node.behaviors.size(), 1u);
    const auto& behavior = node.behaviors[0];
    EXPECT_EQ(behavior.id, "empty_1.behavior.2");
    EXPECT_EQ(behavior.module, "prefab_spawner");
    EXPECT_TRUE(behavior.enabled);
    ASSERT_EQ(behavior.events.size(), 1u);
    EXPECT_EQ(behavior.events[0], "frame.update");

    ASSERT_EQ(behavior.config.size(), 2u);
    EXPECT_EQ(behavior.config[0].name, "prefab_name");
    EXPECT_EQ(behavior.config[0].kind, "string");
    EXPECT_EQ(behavior.config[0].value, "enemy_tank");
    EXPECT_EQ(behavior.config[1].name, "offset_x");
    EXPECT_EQ(behavior.config[1].kind, "int");  // integral number => "int"
    EXPECT_EQ(behavior.config[1].value, "24");
}

// issue #213: an empty grafted set yields a valid-but-empty snapshot (no roots),
// which the editor treats as "nothing to merge" — never a crash or a fallback to
// "everything is a root".
TEST(ProjectSceneSnapshot, BuildsEmptySnapshotFromNoGraftedNodes)
{
    const wz::engine::editor::SceneSnapshot snapshot =
        wz::engine::editor::build_scene_snapshot_from_nodes({});
    EXPECT_EQ(snapshot.schema, "wozzits.scene.v0");
    EXPECT_TRUE(snapshot.roots.empty());
}

TEST(ProjectSnapshot, LoadsInitialEditorSnapshotFromProjectManifest)
{
    TempProjectRoot temp;
    const fs::path project_root = temp.root / "editor_snapshot_project";

    write_text_file(
        manifest_path(project_root),
        R"json({
  "schema": "wozzits.project.v1",
  "formatVersion": 1,
  "name": "Editor Snapshot",
  "scene": "scene.json",
  "asset_graph": "assets.graph.json"
})json");

    write_text_file(
        project_root / "scene.json",
        R"json({
  "schema": "wozzits.scene.v0",
  "name": "main_scene",
  "nodes": [
    {
      "id": "root",
      "visible": true,
      "transform": {
        "translation": [1, 2.125, 3.4567],
        "rotation_quat": [0, 0, 0, 1],
        "scale": [2, 3, 4]
      }
    },
    {
      "id": "camera",
      "parent": "root",
      "camera": {
        "fov_y": 1.0472
      }
    }
  ]
})json");

    write_text_file(
        project_root / "assets.graph.json",
        R"json({
  "schema": "wozzits.scene_editor.assets.graph.v2",
  "version": 2,
  "nodes": []
})json");

    const auto loaded = wz::engine::editor::load_project_snapshot(
        wz::engine::project::ProjectManifestLoadDesc{
            .project_root = project_root.string(),
        });

    ASSERT_TRUE(loaded.ok) << loaded.error;
    EXPECT_EQ(
        loaded.status,
        wz::engine::project::ProjectManifestProbeStatus::Valid);
    EXPECT_EQ(loaded.project_name, "Editor Snapshot");
    ASSERT_TRUE(loaded.asset_graph.ok) << loaded.asset_graph.error;
    ASSERT_TRUE(loaded.scene.ok) << loaded.scene.error;
    ASSERT_EQ(loaded.scene.snapshot.roots.size(), 1u);
    EXPECT_EQ(loaded.scene.snapshot.roots[0].id, "root");
    ASSERT_EQ(loaded.scene.snapshot.roots[0].children.size(), 1u);
    ASSERT_EQ(loaded.scene.snapshot.roots[0].children[0].components.size(), 1u);
    EXPECT_EQ(
        loaded.scene.snapshot.roots[0].children[0].components[0].kind,
        "camera");
    EXPECT_EQ(
        loaded.scene.snapshot.roots[0].children[0].components[0].display_name,
        "Camera");

    const auto blob = wz::engine::editor::project_snapshot_abi_blob(loaded);
    ASSERT_GE(blob.size(), sizeof(WzEditorProjectSnapshot));

    const auto& abi = *reinterpret_cast<const WzEditorProjectSnapshot*>(
        blob.data());
    EXPECT_EQ(abi.abi_version, WZ_ABI_VERSION);
    EXPECT_EQ(abi.ok, 1u);
    EXPECT_EQ(abi.status, WZ_EDITOR_PROJECT_STATUS_VALID);
    EXPECT_EQ(abi_string(blob, abi.project_name), "Editor Snapshot");
    EXPECT_EQ(abi.asset_graph.ok, 1u);
    EXPECT_EQ(abi.scene.ok, 1u);
    ASSERT_EQ(abi.scene.roots.count, 1u);

    const WzEditorSceneNode* roots =
        abi_table<WzEditorSceneNode>(blob, abi.scene.roots);
    EXPECT_EQ(abi_string(blob, roots[0].id), "root");
    EXPECT_EQ(abi_string(blob, roots[0].renderable_source.kind), "none");
    EXPECT_EQ(
        abi_string(blob, roots[0].renderable_source.display_name),
        "none");
    EXPECT_EQ(
        abi_string(blob, roots[0].transform.display_translation_x),
        "1");
    EXPECT_EQ(
        abi_string(blob, roots[0].transform.display_translation_y),
        "2.125");
    EXPECT_EQ(
        abi_string(blob, roots[0].transform.display_translation_z),
        "3.457");
    EXPECT_EQ(
        abi_string(blob, roots[0].transform.display_rotation_x),
        "0");
    EXPECT_EQ(
        abi_string(blob, roots[0].transform.display_scale_z),
        "4");

    ASSERT_EQ(roots[0].children.count, 1u);
    const WzEditorSceneNode* children =
        abi_table<WzEditorSceneNode>(blob, roots[0].children);
    ASSERT_EQ(children[0].components.count, 1u);
    const WzEditorSceneComponent* components =
        abi_table<WzEditorSceneComponent>(blob, children[0].components);
    EXPECT_EQ(abi_string(blob, components[0].kind), "camera");
    EXPECT_EQ(abi_string(blob, components[0].display_name), "Camera");
}

TEST(ProjectSceneSnapshot, SurfacesNodeBehaviorBindingInAbiBlob)
{
    TempProjectRoot temp;
    const fs::path project_root = temp.root / "behavior_snapshot_project";

    write_text_file(
        manifest_path(project_root),
        R"json({
  "schema": "wozzits.project.v1",
  "formatVersion": 1,
  "name": "Behavior Snapshot",
  "scene": "scene.json"
})json");

    write_text_file(
        project_root / "scene.json",
        R"json({
  "schema": "wozzits.scene.v0",
  "name": "behavior_scene",
  "nodes": [
    {
      "id": "root",
      "parent": null,
      "visible": true
    },
    {
      "id": "mover",
      "parent": "root",
      "name": "mover",
      "behavior": {
        "label": "Move up each frame",
        "module": "move_up_on_frame",
        "name": "rise",
        "enabled": true,
        "events": ["frame.update"],
        "config": {
          "speed": 2.5,
          "steps": 3,
          "loop": true,
          "axis": "y"
        }
      }
    }
  ]
})json");

    const auto loaded = wz::engine::editor::load_project_scene_snapshot(
        wz::engine::project::ProjectManifestLoadDesc{
            .project_root = project_root.string(),
        });

    ASSERT_TRUE(loaded.ok) << loaded.error;
    ASSERT_EQ(loaded.snapshot.roots.size(), 1u);
    const auto& root = loaded.snapshot.roots[0];
    ASSERT_EQ(root.children.size(), 1u);
    const auto& mover = root.children[0];

    // Behaviors live in their own structured list, separate from components.
    ASSERT_EQ(mover.behaviors.size(), 1u);
    const auto& behavior = mover.behaviors[0];
    EXPECT_EQ(behavior.module, "move_up_on_frame");
    EXPECT_EQ(behavior.label, "Move up each frame");
    EXPECT_EQ(behavior.name, "rise");
    EXPECT_TRUE(behavior.enabled);
    ASSERT_EQ(behavior.events.size(), 1u);
    EXPECT_EQ(behavior.events[0], "frame.update");
    ASSERT_EQ(behavior.config.size(), 4u);

    // Pack and round-trip through the editor ABI blob.
    wz::engine::editor::ProjectSnapshotLoadResult project_result;
    project_result.ok = true;
    project_result.status =
        wz::engine::project::ProjectManifestProbeStatus::Valid;
    project_result.scene = loaded;

    const auto blob =
        wz::engine::editor::project_snapshot_abi_blob(project_result);
    ASSERT_GE(blob.size(), sizeof(WzEditorProjectSnapshot));

    const auto& abi = *reinterpret_cast<const WzEditorProjectSnapshot*>(
        blob.data());
    EXPECT_EQ(abi.abi_version, WZ_ABI_VERSION);
    ASSERT_EQ(abi.scene.roots.count, 1u);

    const WzEditorSceneNode* roots =
        abi_table<WzEditorSceneNode>(blob, abi.scene.roots);
    ASSERT_EQ(roots[0].children.count, 1u);
    const WzEditorSceneNode* children =
        abi_table<WzEditorSceneNode>(blob, roots[0].children);
    const WzEditorSceneNode& abi_mover = children[0];

    ASSERT_EQ(abi_mover.behaviors.count, 1u);
    const WzEditorSceneBehavior* abi_behaviors =
        abi_table<WzEditorSceneBehavior>(blob, abi_mover.behaviors);
    const WzEditorSceneBehavior& abi_behavior = abi_behaviors[0];

    EXPECT_EQ(abi_string(blob, abi_behavior.module), "move_up_on_frame");
    EXPECT_EQ(abi_string(blob, abi_behavior.label), "Move up each frame");
    EXPECT_EQ(abi_string(blob, abi_behavior.name), "rise");
    EXPECT_EQ(abi_behavior.enabled, 1u);

    ASSERT_EQ(abi_behavior.events.count, 1u);
    const WzEditorStringSpan* abi_events =
        abi_table<WzEditorStringSpan>(blob, abi_behavior.events);
    EXPECT_EQ(abi_string(blob, abi_events[0]), "frame.update");

    // Config is packed in the WzEditorAssetGraphParam shape (name/kind/value).
    ASSERT_EQ(abi_behavior.config.count, 4u);
    const WzEditorAssetGraphParam* abi_config =
        abi_table<WzEditorAssetGraphParam>(blob, abi_behavior.config);

    bool saw_speed = false;
    bool saw_steps = false;
    bool saw_loop = false;
    bool saw_axis = false;
    for (uint64_t i = 0; i < abi_behavior.config.count; ++i) {
        const std::string name = abi_string(blob, abi_config[i].name);
        const std::string kind = abi_string(blob, abi_config[i].kind);
        const std::string value = abi_string(blob, abi_config[i].value);
        if (name == "speed") {
            saw_speed = true;
            EXPECT_EQ(kind, "float");
            EXPECT_EQ(value, "2.5");
        }
        else if (name == "steps") {
            saw_steps = true;
            EXPECT_EQ(kind, "int");
            EXPECT_EQ(value, "3");
        }
        else if (name == "loop") {
            saw_loop = true;
            EXPECT_EQ(kind, "bool");
            EXPECT_EQ(value, "true");
        }
        else if (name == "axis") {
            saw_axis = true;
            EXPECT_EQ(kind, "string");
            EXPECT_EQ(value, "y");
        }
    }
    EXPECT_TRUE(saw_speed);
    EXPECT_TRUE(saw_steps);
    EXPECT_TRUE(saw_loop);
    EXPECT_TRUE(saw_axis);
}

TEST(ProjectSceneSnapshot, SurfacesGlbSceneSourceInAbiBlob)
{
    TempProjectRoot temp;
    const fs::path project_root = temp.root / "scene_source_snapshot_project";

    write_text_file(
        manifest_path(project_root),
        R"json({
  "schema": "wozzits.project.v1",
  "formatVersion": 1,
  "name": "Scene Source Snapshot",
  "scene": "scene.json"
})json");

    // The host node carries a glb_scene_source descriptor (consume_mode
    // "instance", scene_index 0, a base_style, and one style override); the
    // plain node has none, so its scene_source must stay absent.
    write_text_file(
        project_root / "scene.json",
        R"json({
  "schema": "wozzits.scene.v0",
  "name": "scene_source_scene",
  "nodes": [
    {
      "id": "root",
      "parent": null,
      "visible": true
    },
    {
      "id": "tank_host",
      "parent": "root",
      "name": "tank_host",
      "glb_scene_source": {
        "path": "gltf/tank1.glb",
        "scene_index": 0,
        "consume_mode": "instance",
        "base_style": {
          "alpha": 1.0,
          "surface": { "enabled": true, "color": [0.1, 0.2, 0.3, 1.0] },
          "wireframe": { "enabled": false, "color": [0.4, 0.5, 0.6, 1.0] }
        },
        "style_overrides": [
          {
            "mesh_index": 1,
            "style": {
              "alpha": 1.0,
              "surface": { "enabled": false, "color": [0.7, 0.8, 0.9, 1.0] },
              "wireframe": { "enabled": true, "color": [1.0, 0.0, 0.5, 0.25] }
            }
          }
        ]
      }
    },
    {
      "id": "plain",
      "parent": "root",
      "name": "plain"
    }
  ]
})json");

    const auto loaded = wz::engine::editor::load_project_scene_snapshot(
        wz::engine::project::ProjectManifestLoadDesc{
            .project_root = project_root.string(),
        });

    ASSERT_TRUE(loaded.ok) << loaded.error;
    ASSERT_EQ(loaded.snapshot.roots.size(), 1u);
    const auto& root = loaded.snapshot.roots[0];
    ASSERT_EQ(root.children.size(), 2u);

    const auto& host = root.children[0];
    EXPECT_EQ(host.id, "tank_host");
    ASSERT_TRUE(host.scene_source);
    EXPECT_EQ(host.scene_source->kind, "glb");
    EXPECT_EQ(host.scene_source->path, "gltf/tank1.glb");
    EXPECT_EQ(host.scene_source->consume_mode, "instance");
    EXPECT_EQ(host.scene_source->scene_index, 0u);
    EXPECT_EQ(host.scene_source->style_override_count, 1u);
    EXPECT_TRUE(host.scene_source->has_base_style);

    // Phase 3b-2: the base style's surface/wireframe subset round-trips.
    EXPECT_TRUE(host.scene_source->base_style.surface_enabled);
    EXPECT_FLOAT_EQ(host.scene_source->base_style.surface_rgba[0], 0.1f);
    EXPECT_FLOAT_EQ(host.scene_source->base_style.surface_rgba[1], 0.2f);
    EXPECT_FLOAT_EQ(host.scene_source->base_style.surface_rgba[2], 0.3f);
    EXPECT_FLOAT_EQ(host.scene_source->base_style.surface_rgba[3], 1.0f);
    EXPECT_FALSE(host.scene_source->base_style.wireframe_enabled);
    EXPECT_FLOAT_EQ(host.scene_source->base_style.wireframe_rgba[0], 0.4f);

    // ...and the one per-mesh override (mesh_index 1) round-trips.
    ASSERT_EQ(host.scene_source->style_overrides.size(), 1u);
    const auto& ov = host.scene_source->style_overrides[0];
    EXPECT_EQ(ov.mesh_index, 1u);
    EXPECT_FALSE(ov.style.surface_enabled);
    EXPECT_FLOAT_EQ(ov.style.surface_rgba[0], 0.7f);
    EXPECT_TRUE(ov.style.wireframe_enabled);
    EXPECT_FLOAT_EQ(ov.style.wireframe_rgba[0], 1.0f);
    EXPECT_FLOAT_EQ(ov.style.wireframe_rgba[3], 0.25f);

    const auto& plain = root.children[1];
    EXPECT_EQ(plain.id, "plain");
    EXPECT_EQ(plain.scene_source, std::nullopt);

    // Pack and round-trip through the editor ABI blob.
    wz::engine::editor::ProjectSnapshotLoadResult project_result;
    project_result.ok = true;
    project_result.status =
        wz::engine::project::ProjectManifestProbeStatus::Valid;
    project_result.scene = loaded;

    const auto blob =
        wz::engine::editor::project_snapshot_abi_blob(project_result);
    ASSERT_GE(blob.size(), sizeof(WzEditorProjectSnapshot));

    const auto& abi = *reinterpret_cast<const WzEditorProjectSnapshot*>(
        blob.data());
    EXPECT_EQ(abi.abi_version, WZ_ABI_VERSION);
    ASSERT_EQ(abi.scene.roots.count, 1u);

    const WzEditorSceneNode* roots =
        abi_table<WzEditorSceneNode>(blob, abi.scene.roots);
    ASSERT_EQ(roots[0].children.count, 2u);
    const WzEditorSceneNode* children =
        abi_table<WzEditorSceneNode>(blob, roots[0].children);

    const WzEditorSceneNode& abi_host = children[0];
    EXPECT_EQ(abi_string(blob, abi_host.id), "tank_host");
    EXPECT_NE(
        abi_host.flags & WZ_EDITOR_SCENE_NODE_HAS_SCENE_SOURCE,
        0u);
    EXPECT_EQ(abi_string(blob, abi_host.scene_source.kind), "glb");
    EXPECT_EQ(abi_string(blob, abi_host.scene_source.path), "gltf/tank1.glb");
    EXPECT_EQ(
        abi_string(blob, abi_host.scene_source.consume_mode),
        "instance");
    EXPECT_EQ(abi_host.scene_source.scene_index, 0u);
    EXPECT_EQ(abi_host.scene_source.style_override_count, 1u);
    EXPECT_EQ(abi_host.scene_source.has_base_style, 1u);

    // Phase 3b-2: the base style + override table round-trip through the ABI blob.
    EXPECT_EQ(abi_host.scene_source.base_style.surface_enabled, 1u);
    EXPECT_FLOAT_EQ(abi_host.scene_source.base_style.surface_rgba[0], 0.1f);
    EXPECT_FLOAT_EQ(abi_host.scene_source.base_style.surface_rgba[2], 0.3f);
    EXPECT_EQ(abi_host.scene_source.base_style.wireframe_enabled, 0u);
    EXPECT_FLOAT_EQ(abi_host.scene_source.base_style.wireframe_rgba[0], 0.4f);

    ASSERT_EQ(abi_host.scene_source.style_overrides.count, 1u);
    const WzEditorGlbStyleOverride* abi_overrides =
        abi_table<WzEditorGlbStyleOverride>(
            blob, abi_host.scene_source.style_overrides);
    EXPECT_EQ(abi_overrides[0].mesh_index, 1u);
    EXPECT_EQ(abi_overrides[0].style.surface_enabled, 0u);
    EXPECT_FLOAT_EQ(abi_overrides[0].style.surface_rgba[0], 0.7f);
    EXPECT_EQ(abi_overrides[0].style.wireframe_enabled, 1u);
    EXPECT_FLOAT_EQ(abi_overrides[0].style.wireframe_rgba[3], 0.25f);

    const WzEditorSceneNode& abi_plain = children[1];
    EXPECT_EQ(abi_string(blob, abi_plain.id), "plain");
    EXPECT_EQ(
        abi_plain.flags & WZ_EDITOR_SCENE_NODE_HAS_SCENE_SOURCE,
        0u);
}

// The v26 snapshot surfaces the authored render-binding node ids (issue #213) —
// the "Subtree from asset" scene_source ref and the render-binding geometry +
// render-program refs — so the editor's inspector reveals + pre-selects those
// sections from persisted state. A node with none keeps all three flags clear.
TEST(ProjectSceneSnapshot, SurfacesRenderBindingRefsInAbiBlob)
{
    TempProjectRoot temp;
    const fs::path project_root = temp.root / "render_binding_ref_project";

    write_text_file(
        manifest_path(project_root),
        R"json({
  "schema": "wozzits.project.v1",
  "formatVersion": 1,
  "name": "Render Binding Refs",
  "scene": "scene.json"
})json");

    // "binding" carries geometry + render_program refs; "subtree" a scene_source
    // ref; "plain" none.
    write_text_file(
        project_root / "scene.json",
        R"json({
  "schema": "wozzits.scene.v0",
  "name": "render_binding_scene",
  "nodes": [
    { "id": "root", "parent": null, "visible": true },
    {
      "id": "binding",
      "parent": "root",
      "geometry": { "asset_graph_node_id": 9 },
      "render_program": { "asset_graph_node_id": 10 }
    },
    {
      "id": "subtree",
      "parent": "root",
      "scene_source": { "asset_graph_node_id": 42 }
    },
    { "id": "plain", "parent": "root" }
  ]
})json");

    const auto loaded = wz::engine::editor::load_project_scene_snapshot(
        wz::engine::project::ProjectManifestLoadDesc{
            .project_root = project_root.string(),
        });

    ASSERT_TRUE(loaded.ok) << loaded.error;
    ASSERT_EQ(loaded.snapshot.roots.size(), 1u);
    const auto& root = loaded.snapshot.roots[0];
    ASSERT_EQ(root.children.size(), 3u);

    const auto& binding = root.children[0];
    EXPECT_EQ(binding.id, "binding");
    ASSERT_TRUE(binding.geometry_node_id);
    EXPECT_EQ(*binding.geometry_node_id, 9u);
    ASSERT_TRUE(binding.render_program_node_id);
    EXPECT_EQ(*binding.render_program_node_id, 10u);
    EXPECT_EQ(binding.scene_source_node_id, std::nullopt);

    const auto& subtree = root.children[1];
    EXPECT_EQ(subtree.id, "subtree");
    ASSERT_TRUE(subtree.scene_source_node_id);
    EXPECT_EQ(*subtree.scene_source_node_id, 42u);
    EXPECT_EQ(subtree.geometry_node_id, std::nullopt);

    const auto& plain = root.children[2];
    EXPECT_EQ(plain.id, "plain");
    EXPECT_EQ(plain.geometry_node_id, std::nullopt);
    EXPECT_EQ(plain.render_program_node_id, std::nullopt);
    EXPECT_EQ(plain.scene_source_node_id, std::nullopt);

    // Round-trip through the editor ABI blob: flags + ids.
    wz::engine::editor::ProjectSnapshotLoadResult project_result;
    project_result.ok = true;
    project_result.status =
        wz::engine::project::ProjectManifestProbeStatus::Valid;
    project_result.scene = loaded;

    const auto blob =
        wz::engine::editor::project_snapshot_abi_blob(project_result);
    const auto& abi = *reinterpret_cast<const WzEditorProjectSnapshot*>(
        blob.data());
    EXPECT_EQ(abi.abi_version, WZ_ABI_VERSION);

    const WzEditorSceneNode* roots =
        abi_table<WzEditorSceneNode>(blob, abi.scene.roots);
    const WzEditorSceneNode* children =
        abi_table<WzEditorSceneNode>(blob, roots[0].children);

    const WzEditorSceneNode& abi_binding = children[0];
    EXPECT_NE(abi_binding.flags & WZ_EDITOR_SCENE_NODE_HAS_GEOMETRY, 0u);
    EXPECT_NE(abi_binding.flags & WZ_EDITOR_SCENE_NODE_HAS_RENDER_PROGRAM, 0u);
    EXPECT_EQ(
        abi_binding.flags & WZ_EDITOR_SCENE_NODE_HAS_SCENE_SOURCE_REF, 0u);
    EXPECT_EQ(abi_binding.geometry_node_id, 9u);
    EXPECT_EQ(abi_binding.render_program_node_id, 10u);

    const WzEditorSceneNode& abi_subtree = children[1];
    EXPECT_NE(
        abi_subtree.flags & WZ_EDITOR_SCENE_NODE_HAS_SCENE_SOURCE_REF, 0u);
    EXPECT_EQ(abi_subtree.flags & WZ_EDITOR_SCENE_NODE_HAS_GEOMETRY, 0u);
    EXPECT_EQ(abi_subtree.scene_source_node_id, 42u);

    const WzEditorSceneNode& abi_plain = children[2];
    EXPECT_EQ(abi_plain.flags & WZ_EDITOR_SCENE_NODE_HAS_GEOMETRY, 0u);
    EXPECT_EQ(abi_plain.flags & WZ_EDITOR_SCENE_NODE_HAS_RENDER_PROGRAM, 0u);
    EXPECT_EQ(
        abi_plain.flags & WZ_EDITOR_SCENE_NODE_HAS_SCENE_SOURCE_REF, 0u);
}

// The v27 snapshot surfaces the persisted Collision/Motion component field values
// (read-back gap fix) so the editor's inspector restores them on select + after
// reload instead of resetting to defaults. A node with neither component keeps
// both HAS_* flags clear.
TEST(ProjectSceneSnapshot, SurfacesCollisionAndMotionFieldsInAbiBlob)
{
    TempProjectRoot temp;
    const fs::path project_root = temp.root / "collision_motion_snapshot_project";

    write_text_file(
        manifest_path(project_root),
        R"json({
  "schema": "wozzits.project.v1",
  "formatVersion": 1,
  "name": "Collision Motion Fields",
  "scene": "scene.json"
})json");

    // "actor" carries both collision (with a node ref + constrain_movement) and
    // motion (terrain-stick fields); "plain" carries neither.
    write_text_file(
        project_root / "scene.json",
        R"json({
  "schema": "wozzits.scene.v0",
  "name": "collision_motion_scene",
  "nodes": [
    { "id": "root", "parent": null, "visible": true },
    {
      "id": "actor",
      "parent": "root",
      "collision": {
        "collision_asset_node_id": 17,
        "constrain_movement": true
      },
      "motion": {
        "terrain_constrained": true,
        "terrain_ride_height": 2.5,
        "terrain_footprint_radius": 0.75,
        "terrain_align_to_surface": true,
        "terrain_alignment_strength": 0.5
      }
    },
    { "id": "plain", "parent": "root" }
  ]
})json");

    const auto loaded = wz::engine::editor::load_project_scene_snapshot(
        wz::engine::project::ProjectManifestLoadDesc{
            .project_root = project_root.string(),
        });

    ASSERT_TRUE(loaded.ok) << loaded.error;
    ASSERT_EQ(loaded.snapshot.roots.size(), 1u);
    const auto& root = loaded.snapshot.roots[0];
    ASSERT_EQ(root.children.size(), 2u);

    const auto& actor = root.children[0];
    EXPECT_EQ(actor.id, "actor");
    ASSERT_TRUE(actor.collision);
    ASSERT_TRUE(actor.collision->collision_asset_node_id);
    EXPECT_EQ(*actor.collision->collision_asset_node_id, 17u);
    EXPECT_TRUE(actor.collision->constrain_movement);
    ASSERT_TRUE(actor.motion);
    EXPECT_TRUE(actor.motion->terrain_constrained);
    EXPECT_FLOAT_EQ(actor.motion->ride_height, 2.5f);
    EXPECT_FLOAT_EQ(actor.motion->footprint_radius, 0.75f);
    EXPECT_TRUE(actor.motion->align_to_surface);
    EXPECT_FLOAT_EQ(actor.motion->alignment_strength, 0.5f);

    const auto& plain = root.children[1];
    EXPECT_EQ(plain.id, "plain");
    EXPECT_EQ(plain.collision, std::nullopt);
    EXPECT_EQ(plain.motion, std::nullopt);

    // Round-trip through the editor ABI blob: flags + packed field values.
    wz::engine::editor::ProjectSnapshotLoadResult project_result;
    project_result.ok = true;
    project_result.status =
        wz::engine::project::ProjectManifestProbeStatus::Valid;
    project_result.scene = loaded;

    const auto blob =
        wz::engine::editor::project_snapshot_abi_blob(project_result);
    const auto& abi = *reinterpret_cast<const WzEditorProjectSnapshot*>(
        blob.data());
    EXPECT_EQ(abi.abi_version, WZ_ABI_VERSION);

    const WzEditorSceneNode* roots =
        abi_table<WzEditorSceneNode>(blob, abi.scene.roots);
    const WzEditorSceneNode* children =
        abi_table<WzEditorSceneNode>(blob, roots[0].children);

    const WzEditorSceneNode& abi_actor = children[0];
    EXPECT_NE(abi_actor.flags & WZ_EDITOR_SCENE_NODE_HAS_COLLISION, 0u);
    EXPECT_NE(abi_actor.flags & WZ_EDITOR_SCENE_NODE_HAS_MOTION, 0u);
    EXPECT_EQ(abi_actor.collision.has_collision_ref, 1u);
    EXPECT_EQ(abi_actor.collision.collision_asset_node_id, 17u);
    EXPECT_EQ(abi_actor.collision.constrain_movement, 1u);
    EXPECT_EQ(abi_actor.motion.terrain_constrained, 1u);
    EXPECT_EQ(abi_actor.motion.align_to_surface, 1u);
    EXPECT_FLOAT_EQ(abi_actor.motion.ride_height, 2.5f);
    EXPECT_FLOAT_EQ(abi_actor.motion.footprint_radius, 0.75f);
    EXPECT_FLOAT_EQ(abi_actor.motion.alignment_strength, 0.5f);

    const WzEditorSceneNode& abi_plain = children[1];
    EXPECT_EQ(abi_plain.flags & WZ_EDITOR_SCENE_NODE_HAS_COLLISION, 0u);
    EXPECT_EQ(abi_plain.flags & WZ_EDITOR_SCENE_NODE_HAS_MOTION, 0u);
}

TEST(AssetGraphSnapshot, ShowsTypedPortsAndPersistsNodeLayout)
{
    TempProjectRoot temp;
    const fs::path project_root = temp.root / "asset_graph_layout_project";

    write_text_file(
        manifest_path(project_root),
        R"json({
  "schema": "wozzits.project.v1",
  "formatVersion": 1,
  "name": "Asset Graph Layout",
  "asset_graph": "assets.graph.json"
})json");

    write_text_file(
        project_root / "assets.graph.json",
        R"json({
  "schema": "wozzits.scene_editor.assets.graph.v2",
  "version": 2,
  "nodes": [
    {
      "node_id": 1,
      "type": 7,
      "schema": "0xf11eca55e7000002",
      "stage": 0,
      "residency": 1,
      "kind": 0,
      "demand_root": 0,
      "deps": []
    },
    {
      "node_id": 2,
      "type": 4,
      "schema": "0xf11eca55e7000100",
      "stage": 0,
      "residency": 1,
      "kind": 0,
      "demand_root": 0,
      "deps": [
        {
          "from_node_id": 1,
          "to_input_port": 0
        }
      ]
    }
  ],
  "layout": {
    "version": 2,
    "zoom": 1.25,
    "nodes": [
      {
        "node_id": 1,
        "x": 5,
        "y": 6
      }
    ]
  }
})json");

    const wz::engine::project::ProjectManifestLoadDesc desc{
        .project_root = project_root.string(),
    };

    const auto loaded =
        wz::engine::editor::load_project_asset_graph_snapshot(desc);
    ASSERT_TRUE(loaded.ok) << loaded.error;
    EXPECT_DOUBLE_EQ(loaded.snapshot.zoom, 1.25);
    ASSERT_EQ(loaded.snapshot.nodes.size(), 2u);

    const auto shader_source = std::find_if(
        loaded.snapshot.nodes.begin(),
        loaded.snapshot.nodes.end(),
        [](const wz::engine::editor::AssetGraphSnapshotNode& node)
        {
            return node.id == 1u;
        });
    ASSERT_NE(shader_source, loaded.snapshot.nodes.end());
    ASSERT_EQ(shader_source->output_ports.size(), 1u);
    EXPECT_EQ(shader_source->output_ports[0].label, "Shader source");
    EXPECT_EQ(shader_source->output_ports[0].type_name, "Shader source");
    EXPECT_DOUBLE_EQ(shader_source->x, 5.0);
    EXPECT_DOUBLE_EQ(shader_source->y, 6.0);

    const auto shader = std::find_if(
        loaded.snapshot.nodes.begin(),
        loaded.snapshot.nodes.end(),
        [](const wz::engine::editor::AssetGraphSnapshotNode& node)
        {
            return node.id == 2u;
        });
    ASSERT_NE(shader, loaded.snapshot.nodes.end());
    ASSERT_EQ(shader->input_ports.size(), 1u);
    EXPECT_EQ(shader->input_ports[0].label, "Shader source");
    EXPECT_EQ(shader->input_ports[0].type_name, "Shader source");
    ASSERT_EQ(shader->output_ports.size(), 1u);
    EXPECT_EQ(shader->output_ports[0].label, "Shader");
    EXPECT_EQ(shader->output_ports[0].type_name, "Shader");

    const auto updated =
        wz::engine::editor::update_project_asset_graph_node_layout(
            desc,
            2u,
            123.0,
            234.0);
    ASSERT_TRUE(updated.ok) << updated.error;

    const auto reloaded =
        wz::engine::editor::load_project_asset_graph_snapshot(desc);
    ASSERT_TRUE(reloaded.ok) << reloaded.error;

    const auto moved = std::find_if(
        reloaded.snapshot.nodes.begin(),
        reloaded.snapshot.nodes.end(),
        [](const wz::engine::editor::AssetGraphSnapshotNode& node)
        {
            return node.id == 2u;
        });
    ASSERT_NE(moved, reloaded.snapshot.nodes.end());
    EXPECT_DOUBLE_EQ(moved->x, 123.0);
    EXPECT_DOUBLE_EQ(moved->y, 234.0);

    const auto zoom_updated =
        wz::engine::editor::update_project_asset_graph_zoom(
            desc,
            1.75);
    ASSERT_TRUE(zoom_updated.ok) << zoom_updated.error;

    const auto zoom_reloaded =
        wz::engine::editor::load_project_asset_graph_snapshot(desc);
    ASSERT_TRUE(zoom_reloaded.ok) << zoom_reloaded.error;
    EXPECT_DOUBLE_EQ(zoom_reloaded.snapshot.zoom, 1.75);
}

TEST(AssetGraphSnapshot, PacksDiagnosticDisplayNamesForEditorAbi)
{
    wz::engine::editor::AssetGraphSnapshot snapshot{};
    snapshot.schema = "test";

    wz::engine::editor::AssetGraphSnapshotDiagnostic diagnostic{};
    diagnostic.severity = wz::asset::AssetGraphDraftValidationSeverity::Error;
    diagnostic.code = wz::asset::AssetGraphDraftValidationCode::TypeMismatch;
    diagnostic.severity_name =
        std::string(wz::asset::asset_graph_draft_validation_severity_name(
            diagnostic.severity));
    diagnostic.code_name =
        std::string(wz::asset::asset_graph_draft_validation_code_name(
            diagnostic.code));
    diagnostic.node = 1u;
    diagnostic.input_port = 0u;
    diagnostic.message = "shader input expects Shader source";

    wz::engine::editor::AssetGraphSnapshotNode node{};
    node.id = 1u;
    node.diagnostics.push_back(std::move(diagnostic));
    snapshot.nodes.push_back(std::move(node));

    const auto blob = wz::engine::editor::asset_graph_snapshot_abi_blob(snapshot);
    ASSERT_GE(blob.size(), sizeof(WzEditorAssetGraphSnapshot));

    const auto& abi = *reinterpret_cast<const WzEditorAssetGraphSnapshot*>(
        blob.data());
    ASSERT_EQ(abi.ok, 1u);
    ASSERT_EQ(abi.nodes.count, 1u);

    bool found_abi_diagnostic = false;
    const WzEditorAssetGraphNode* nodes =
        abi_table<WzEditorAssetGraphNode>(blob, abi.nodes);
    for (uint64_t node_index = 0; node_index < abi.nodes.count; ++node_index) {
        const WzEditorAssetGraphNode& node = nodes[node_index];
        const WzEditorAssetGraphDiagnostic* diagnostics =
            abi_table<WzEditorAssetGraphDiagnostic>(blob, node.diagnostics);
        for (uint64_t diagnostic_index = 0;
             diagnostic_index < node.diagnostics.count;
             ++diagnostic_index)
        {
            const WzEditorAssetGraphDiagnostic& diagnostic =
                diagnostics[diagnostic_index];
            if (abi_string(blob, diagnostic.code_name) != "TypeMismatch") {
                continue;
            }

            found_abi_diagnostic = true;
            EXPECT_EQ(
                diagnostic.severity,
                static_cast<uint32_t>(
                    wz::asset::AssetGraphDraftValidationSeverity::Error));
            EXPECT_EQ(
                diagnostic.code,
                static_cast<uint32_t>(
                    wz::asset::AssetGraphDraftValidationCode::TypeMismatch));
            EXPECT_EQ(abi_string(blob, diagnostic.severity_name), "error");
            EXPECT_FALSE(abi_string(blob, diagnostic.message).empty());
        }
    }
    EXPECT_TRUE(found_abi_diagnostic);
}

TEST(ProjectSnapshot, PacksMissingProjectForBootstrap)
{
    TempProjectRoot temp;
    const fs::path project_root = temp.root / "missing_project";

    const auto loaded = wz::engine::editor::load_project_snapshot(
        wz::engine::project::ProjectManifestLoadDesc{
            .project_root = project_root.string(),
        });

    EXPECT_FALSE(loaded.ok);
    EXPECT_EQ(
        loaded.status,
        wz::engine::project::ProjectManifestProbeStatus::Missing);
    EXPECT_FALSE(loaded.error.empty());

    const auto blob = wz::engine::editor::project_snapshot_abi_blob(loaded);
    ASSERT_GE(blob.size(), sizeof(WzEditorProjectSnapshot));

    const auto& abi = *reinterpret_cast<const WzEditorProjectSnapshot*>(
        blob.data());
    EXPECT_EQ(abi.ok, 0u);
    EXPECT_EQ(abi.status, WZ_EDITOR_PROJECT_STATUS_MISSING);
    EXPECT_FALSE(abi_string(blob, abi.error).empty());
    EXPECT_EQ(abi.asset_graph.ok, 0u);
    EXPECT_EQ(abi.scene.ok, 0u);
}

TEST(ProjectSnapshot, PacksCreateProjectResponseForEditorAbi)
{
    TempProjectRoot temp;
    const fs::path project_root = temp.root / "created_from_abi";

    const auto created = wz::engine::project::create_project_manifest(
        wz::engine::project::ProjectManifestCreateDesc{
            .project_root = project_root.string(),
            .name = "Created From ABI",
        });

    ASSERT_TRUE(created.ok) << created.error;
    EXPECT_TRUE(created.created);

    const auto blob = wz::engine::editor::project_create_abi_blob(created);
    ASSERT_GE(blob.size(), sizeof(WzEditorProjectCreate));

    const auto& abi = *reinterpret_cast<const WzEditorProjectCreate*>(
        blob.data());
    EXPECT_EQ(abi.abi_version, WZ_ABI_VERSION);
    EXPECT_EQ(abi.ok, 1u);
    EXPECT_EQ(abi.status, WZ_EDITOR_PROJECT_STATUS_VALID);
    EXPECT_EQ(abi.created, 1u);
    EXPECT_TRUE(abi_string(blob, abi.error).empty());
}

namespace
{
    // tank1.glb (the glb_scene_source fixture's GLB) scene-0 hierarchy, observed
    // by parsing the file: a depth-first single root chain body -> turret -> gun,
    // each carrying a mesh. The same bytes ship in resources/gltf/tank1.glb; this
    // exercises the on-demand import path that backs wz_import_glb_scene_hierarchy
    // (read_file -> import_gltf_scene -> glb_scene_hierarchy_abi_blob) device-free.
    const char* kTank1Glb = WZ_TEST_FIXTURE_DIR "/gltf/tank1.glb";

    wz::engine::assets::ImportedGLTFScene import_glb_scene(
        const std::string& path,
        uint32_t scene_index,
        std::string& error)
    {
        const auto file = wz::fs::read_file(path);
        EXPECT_TRUE(static_cast<bool>(file)) << "failed to read " << path;
        wz::engine::assets::ImportedGLTFScene imported{};
        if (!file) {
            error = "read failed";
            return imported;
        }
        EXPECT_TRUE(wz::engine::assets::import_gltf_scene(
            file.value.data(),
            file.value.size(),
            wz::engine::assets::GLTFSceneImportOptions{
                .scene_index = scene_index,
            },
            imported,
            &error)) << error;
        return imported;
    }
}

TEST(GlbSceneHierarchy, ImportsTank1SceneZeroHierarchy)
{
    std::string error;
    const wz::engine::assets::ImportedGLTFScene imported =
        import_glb_scene(kTank1Glb, 0u, error);

    const std::vector<uint8_t> blob =
        wz::engine::editor::glb_scene_hierarchy_abi_blob(true, "", imported);
    ASSERT_GE(blob.size(), sizeof(WzEditorGlbSceneHierarchy));

    const auto& root =
        *reinterpret_cast<const WzEditorGlbSceneHierarchy*>(blob.data());
    EXPECT_EQ(root.abi_version, WZ_ABI_VERSION);
    EXPECT_EQ(root.ok, 1u);
    EXPECT_TRUE(abi_string(blob, root.error).empty());
    EXPECT_EQ(root.scene_index, 0u);
    EXPECT_EQ(abi_string(blob, root.scene_name), "Scene");

    ASSERT_EQ(root.components.count, 3u);
    const auto* components =
        abi_table<WzEditorGlbComponent>(blob, root.components);

    // Depth-first order from the single root: body -> turret -> gun.
    const WzEditorGlbComponent& body = components[0];
    EXPECT_EQ(abi_string(blob, body.id), "body");
    EXPECT_EQ(abi_string(blob, body.name), "body");
    EXPECT_EQ(body.node_index, 2u);
    EXPECT_EQ(body.flags & WZ_EDITOR_GLB_COMPONENT_HAS_PARENT, 0u);
    EXPECT_NE(body.flags & WZ_EDITOR_GLB_COMPONENT_HAS_MESH, 0u);

    const WzEditorGlbComponent& turret = components[1];
    EXPECT_EQ(abi_string(blob, turret.id), "turret");
    EXPECT_NE(turret.flags & WZ_EDITOR_GLB_COMPONENT_HAS_PARENT, 0u);
    EXPECT_EQ(abi_string(blob, turret.parent_id), "body");
    EXPECT_NE(turret.flags & WZ_EDITOR_GLB_COMPONENT_HAS_MESH, 0u);

    const WzEditorGlbComponent& gun = components[2];
    EXPECT_EQ(abi_string(blob, gun.id), "gun");
    EXPECT_NE(gun.flags & WZ_EDITOR_GLB_COMPONENT_HAS_PARENT, 0u);
    EXPECT_EQ(abi_string(blob, gun.parent_id), "turret");
    EXPECT_NE(gun.flags & WZ_EDITOR_GLB_COMPONENT_HAS_MESH, 0u);
}

TEST(GlbSceneHierarchy, FailedImportYieldsNotOkBlob)
{
    // The ABI function packs this shape on a null/empty path, an unreadable file,
    // or an import failure: ok=0, an error string, and an empty component table.
    const std::vector<uint8_t> blob =
        wz::engine::editor::glb_scene_hierarchy_abi_blob(
            false,
            "failed to read GLB file: does_not_exist.glb",
            wz::engine::assets::ImportedGLTFScene{});
    ASSERT_GE(blob.size(), sizeof(WzEditorGlbSceneHierarchy));

    const auto& root =
        *reinterpret_cast<const WzEditorGlbSceneHierarchy*>(blob.data());
    EXPECT_EQ(root.abi_version, WZ_ABI_VERSION);
    EXPECT_EQ(root.ok, 0u);
    EXPECT_EQ(
        abi_string(blob, root.error),
        "failed to read GLB file: does_not_exist.glb");
    EXPECT_EQ(root.components.count, 0u);
}
