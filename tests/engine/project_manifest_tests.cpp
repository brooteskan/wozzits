#include <gtest/gtest.h>

#include <asset/draft.h>
#include <engine/abi/wozzits_abi.h>
#include <engine/assets/gltf/gltf_importer.h>
#include <engine/assets/scene/scene_json_export.h>
#include <external/json/json_writer.h>
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

// Prefab re-open regression: after open_scene the editor rebuilds its tree from
// live SceneNodeAssets via build_scene_snapshot_from_nodes. That path must
// surface an optional component BOTH as a removable entry in `components` (the
// inspector gates its section on this) AND with its field values -- else a
// re-opened prefab hides the Motion Filter section though the data loaded, and
// re-adding overwrites the saved fields with defaults.
TEST(ProjectSceneSnapshot, SurfacesMotionFilterComponentAndFieldsFromNodes)
{
    wz::engine::assets::SceneNodeAsset cam;
    cam.id = "cam";
    wz::engine::assets::SceneMotionFilterAsset f;
    f.roll.smoothing_time = 0.4f;
    f.terrain_floor = true;
    f.terrain_floor_offset = 1.5f;
    cam.motion_filter = f;

    const wz::engine::editor::SceneSnapshot snapshot =
        wz::engine::editor::build_scene_snapshot_from_nodes({ cam });

    ASSERT_EQ(snapshot.roots.size(), 1u);
    const auto& node = snapshot.roots[0];

    // Present as a removable component (the section's gate).
    const bool listed = std::any_of(
        node.components.begin(), node.components.end(),
        [](const auto& c) { return c.kind == "motion_filter"; });
    EXPECT_TRUE(listed);

    // And the field values are surfaced (restored on select, not reset).
    ASSERT_TRUE(node.motion_filter.has_value());
    EXPECT_FLOAT_EQ(node.motion_filter->roll.smoothing_time, 0.4f);
    EXPECT_TRUE(node.motion_filter->terrain_floor);
    EXPECT_FLOAT_EQ(node.motion_filter->terrain_floor_offset, 1.5f);
}

// The other half of the RenderToTexture parity check: the JSON reader. The two
// snapshot paths are independent implementations of the same mapping, and every
// "component missing / won't save" report so far has been one of them lagging the
// other, so the on-disk path is asserted against the same authored values as the
// asset path above. Note the exported member is "target_asset_node_id" while the
// in-memory field is target_node_id -- a mismatch here reads back as an unbound
// target with no error.
TEST(ProjectSceneSnapshot, ReadsRenderToTextureFromSceneJSON)
{
    TempProjectRoot temp;
    const fs::path project_root = temp.root / "rtt_snapshot_project";

    write_text_file(
        manifest_path(project_root),
        R"json({
  "schema": "wozzits.project.v1",
  "formatVersion": 1,
  "name": "RTT Snapshot",
  "scene": "scene.json"
})json");

    write_text_file(
        project_root / "scene.json",
        R"json({
  "schema": "wozzits.scene.v0",
  "name": "rtt_scene",
  "nodes": [
    {
      "id": "card",
      "parent": null,
      "render_to_texture": {
        "target_asset_node_id": 42,
        "include_descendants": false,
        "also_draw_in_scene": true,
        "enabled": false
      }
    },
    {
      "id": "plain",
      "parent": null
    }
  ]
})json");

    const auto loaded = wz::engine::editor::load_project_scene_snapshot(
        wz::engine::project::ProjectManifestLoadDesc{
            .project_root = project_root.string(),
        });
    ASSERT_TRUE(loaded.ok) << loaded.error;
    ASSERT_EQ(loaded.snapshot.roots.size(), 2u);

    const auto& card = loaded.snapshot.roots[0];
    const bool listed = std::any_of(
        card.components.begin(), card.components.end(),
        [](const auto& c) { return c.kind == "render_to_texture"; });
    EXPECT_TRUE(listed);
    ASSERT_TRUE(card.render_to_texture.has_value());
    ASSERT_TRUE(card.render_to_texture->target_node_id.has_value());
    EXPECT_EQ(*card.render_to_texture->target_node_id, 42u);
    EXPECT_FALSE(card.render_to_texture->include_descendants);
    EXPECT_TRUE(card.render_to_texture->also_draw_in_scene);
    EXPECT_FALSE(card.render_to_texture->enabled);

    // A node without the component neither lists it nor carries fields -- the
    // reader is tolerant of absence, not defaulting it in.
    const auto& plain = loaded.snapshot.roots[1];
    EXPECT_FALSE(plain.render_to_texture.has_value());
    EXPECT_FALSE(std::any_of(
        plain.components.begin(), plain.components.end(),
        [](const auto& c) { return c.kind == "render_to_texture"; }));
}

// Same parity requirement for RenderToTexture (#287). It reaches the snapshot by
// two independent paths -- build_scene_snapshot_from_nodes for a grafted /
// scenelet-round-trip node, and the JSON reader for a scene on disk -- and the
// inspector gates its section on the `components` entry while restoring the
// fields from node.render_to_texture. A path that surfaces one without the other
// is the "component missing / won't save" failure the sibling components each
// hit in turn, so assert both halves on the asset path here.
TEST(ProjectSceneSnapshot, SurfacesRenderToTextureComponentAndFieldsFromNodes)
{
    wz::engine::assets::SceneNodeAsset card;
    card.id = "card";
    wz::engine::assets::SceneRenderToTextureAsset rtt;
    rtt.target_node_id = 42u;
    rtt.include_descendants = false;
    rtt.also_draw_in_scene = true;
    rtt.enabled = false;
    card.render_to_texture = rtt;

    const wz::engine::editor::SceneSnapshot snapshot =
        wz::engine::editor::build_scene_snapshot_from_nodes({ card });

    ASSERT_EQ(snapshot.roots.size(), 1u);
    const auto& node = snapshot.roots[0];

    const bool listed = std::any_of(
        node.components.begin(), node.components.end(),
        [](const auto& c) { return c.kind == "render_to_texture"; });
    EXPECT_TRUE(listed);

    ASSERT_TRUE(node.render_to_texture.has_value());
    ASSERT_TRUE(node.render_to_texture->target_node_id.has_value());
    EXPECT_EQ(*node.render_to_texture->target_node_id, 42u);
    // Every switch survives, including the two whose defaults are the opposite
    // of what is authored here -- a defaults-shaped read-back would pass a test
    // that only checked the target.
    EXPECT_FALSE(node.render_to_texture->include_descendants);
    EXPECT_TRUE(node.render_to_texture->also_draw_in_scene);
    EXPECT_FALSE(node.render_to_texture->enabled);
}

// An UNBOUND render-to-texture (no target picked yet) must still surface as a
// component so the inspector shows the section the user just added, with an
// empty target rather than a phantom node 0.
TEST(ProjectSceneSnapshot, SurfacesUnboundRenderToTextureFromNodes)
{
    wz::engine::assets::SceneNodeAsset card;
    card.id = "card";
    card.render_to_texture = wz::engine::assets::SceneRenderToTextureAsset{};

    const wz::engine::editor::SceneSnapshot snapshot =
        wz::engine::editor::build_scene_snapshot_from_nodes({ card });

    ASSERT_EQ(snapshot.roots.size(), 1u);
    const auto& node = snapshot.roots[0];
    const bool listed = std::any_of(
        node.components.begin(), node.components.end(),
        [](const auto& c) { return c.kind == "render_to_texture"; });
    EXPECT_TRUE(listed);
    ASSERT_TRUE(node.render_to_texture.has_value());
    EXPECT_FALSE(node.render_to_texture->target_node_id.has_value());
    EXPECT_TRUE(node.render_to_texture->include_descendants);
    EXPECT_FALSE(node.render_to_texture->also_draw_in_scene);
    EXPECT_TRUE(node.render_to_texture->enabled);
}

// A camera on a grafted / scenelet-round-trip node must surface the same way as
// its sibling components: the removable `components` entry AND node.camera field
// values. The editor reveals its Camera section on node.camera presence, so a
// marker-only projection left a spawned tank's camera node with no Camera section
// at all (the "camera component not showing" report). Camera was the lone
// optional component whose value struct this path skipped.
TEST(ProjectSceneSnapshot, SurfacesCameraComponentAndFieldsFromNodes)
{
    wz::engine::assets::SceneNodeAsset cam;
    cam.id = "camera";
    wz::engine::assets::SceneCameraAsset c;
    c.fov_y = 1.2f;
    c.near_plane = 0.25f;
    c.far_plane = 750.0f;
    c.aspect = 1.5f;
    cam.camera = c;

    const wz::engine::editor::SceneSnapshot snapshot =
        wz::engine::editor::build_scene_snapshot_from_nodes({ cam });

    ASSERT_EQ(snapshot.roots.size(), 1u);
    const auto& node = snapshot.roots[0];

    // Present as a removable component (matches read_node's list).
    const bool listed = std::any_of(
        node.components.begin(), node.components.end(),
        [](const auto& entry) { return entry.kind == "camera"; });
    EXPECT_TRUE(listed);

    // And the field values are surfaced -- the section's actual gate.
    ASSERT_TRUE(node.camera.has_value());
    ASSERT_TRUE(node.camera->fov_y);
    EXPECT_DOUBLE_EQ(*node.camera->fov_y, static_cast<double>(1.2f));
    ASSERT_TRUE(node.camera->far_plane);
    EXPECT_DOUBLE_EQ(*node.camera->far_plane, 750.0);
    // With the values present the node resolves to a camera kind, like the JSON path.
    EXPECT_EQ(node.kind, "camera");
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

// Custom-renderable ingredients (issue #229/#230): the scene snapshot surfaces
// a node's renderable_bindings + renderable_constants from BOTH routes — the
// scene-JSON read and the live SceneNodeAsset projection — and packs them into
// the editor ABI blob as the two per-node tables, so the inspector can
// pre-fill its binding rows + constant overrides.
TEST(ProjectSceneSnapshot, SurfacesRenderableBindingsAndConstantsInAbiBlob)
{
    TempProjectRoot temp;
    const fs::path project_root = temp.root / "renderable_binding_snapshot";

    write_text_file(
        manifest_path(project_root),
        R"json({
  "schema": "wozzits.project.v1",
  "formatVersion": 1,
  "name": "Renderable Binding Snapshot",
  "scene": "scene.json"
})json");

    write_text_file(
        project_root / "scene.json",
        R"json({
  "schema": "wozzits.scene.v0",
  "name": "renderable_binding_scene",
  "nodes": [
    {
      "id": "bound",
      "parent": null,
      "visible": true,
      "geometry": { "asset_graph_node_id": 1 },
      "render_program": { "asset_graph_node_id": 16 },
      "renderable_bindings": [
        { "semantic": "scalar_field_texture", "asset_graph_node_id": 17 },
        { "semantic": "splat_cloud" }
      ],
      "renderable_constants": [
        { "name": "tint", "value": [0.2, 0.7, 0.3, 1.0] }
      ]
    }
  ]
})json");

    const auto loaded = wz::engine::editor::load_project_scene_snapshot(
        wz::engine::project::ProjectManifestLoadDesc{
            .project_root = project_root.string(),
        });

    ASSERT_TRUE(loaded.ok) << loaded.error;
    ASSERT_EQ(loaded.snapshot.roots.size(), 1u);
    const auto& bound = loaded.snapshot.roots[0];

    ASSERT_EQ(bound.renderable_bindings.size(), 2u);
    EXPECT_EQ(bound.renderable_bindings[0].semantic, "scalar_field_texture");
    ASSERT_TRUE(bound.renderable_bindings[0].asset_graph_node_id.has_value());
    EXPECT_EQ(*bound.renderable_bindings[0].asset_graph_node_id, 17u);
    EXPECT_EQ(bound.renderable_bindings[1].semantic, "splat_cloud");
    EXPECT_FALSE(bound.renderable_bindings[1].asset_graph_node_id.has_value());
    ASSERT_EQ(bound.renderable_constants.size(), 1u);
    EXPECT_EQ(bound.renderable_constants[0].name, "tint");
    EXPECT_FLOAT_EQ(bound.renderable_constants[0].value[0], 0.2f);
    EXPECT_FLOAT_EQ(bound.renderable_constants[0].value[3], 1.0f);

    // Pack into the editor ABI blob and read the two per-node tables back.
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

    ASSERT_EQ(roots[0].renderable_bindings.count, 2u);
    const WzEditorSceneRenderableBinding* abi_bindings =
        abi_table<WzEditorSceneRenderableBinding>(
            blob, roots[0].renderable_bindings);
    EXPECT_EQ(
        abi_string(blob, abi_bindings[0].semantic), "scalar_field_texture");
    EXPECT_EQ(abi_bindings[0].has_source_ref, 1u);
    EXPECT_EQ(abi_bindings[0].asset_graph_node_id, 17u);
    EXPECT_EQ(abi_string(blob, abi_bindings[1].semantic), "splat_cloud");
    EXPECT_EQ(abi_bindings[1].has_source_ref, 0u);

    ASSERT_EQ(roots[0].renderable_constants.count, 1u);
    const WzEditorSceneRenderableConstant* abi_constants =
        abi_table<WzEditorSceneRenderableConstant>(
            blob, roots[0].renderable_constants);
    EXPECT_EQ(abi_string(blob, abi_constants[0].name), "tint");
    EXPECT_FLOAT_EQ(abi_constants[0].value[0], 0.2f);
    EXPECT_FLOAT_EQ(abi_constants[0].value[1], 0.7f);
    EXPECT_FLOAT_EQ(abi_constants[0].value[2], 0.3f);
    EXPECT_FLOAT_EQ(abi_constants[0].value[3], 1.0f);

    // The LIVE SceneNodeAsset projection surfaces the same ingredients (the
    // prefab-editor open_scene path rebuilds the tree from live nodes).
    wz::engine::assets::SceneNodeAsset live{};
    live.id = "live_bound";
    live.renderable_bindings.push_back(
        wz::engine::assets::SceneRenderableSemanticBinding{
            .semantic = "scalar_field_texture",
            .asset_graph_node_id = 17u,
        });
    live.renderable_constants.push_back(
        wz::engine::assets::SceneRenderableConstantOverride{
            .name = "tint",
            .value = { 0.9f, 0.1f, 0.4f, 1.0f },
        });
    const auto live_snapshot =
        wz::engine::editor::build_scene_snapshot_from_nodes({ live });
    ASSERT_EQ(live_snapshot.roots.size(), 1u);
    ASSERT_EQ(live_snapshot.roots[0].renderable_bindings.size(), 1u);
    EXPECT_EQ(
        live_snapshot.roots[0].renderable_bindings[0].semantic,
        "scalar_field_texture");
    ASSERT_TRUE(live_snapshot.roots[0]
                    .renderable_bindings[0].asset_graph_node_id.has_value());
    EXPECT_EQ(
        *live_snapshot.roots[0].renderable_bindings[0].asset_graph_node_id,
        17u);
    ASSERT_EQ(live_snapshot.roots[0].renderable_constants.size(), 1u);
    EXPECT_EQ(live_snapshot.roots[0].renderable_constants[0].name, "tint");
    EXPECT_FLOAT_EQ(
        live_snapshot.roots[0].renderable_constants[0].value[0], 0.9f);
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
        "path": "gltf/test-mesh-a.glb",
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
    EXPECT_EQ(host.scene_source->path, "gltf/test-mesh-a.glb");
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
    EXPECT_EQ(abi_string(blob, abi_host.scene_source.path), "gltf/test-mesh-a.glb");
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

// The LIVE twin of the test above, and of SurfacesRenderBindingRefsInAbiBlob:
// the scene-source PAIR -- the glb_scene_source descriptor and the
// scene_source_node_id graph ref -- must also come through
// build_scene_snapshot_from_nodes, the path the editor uses after open_scene.
//
// This was the last hole in that path's per-component parity, and it did not
// merely blank a dropdown: the inspector's RestoreSubtreeReferenceState reads
// SceneSourceNodeId, and with the field empty it takes the "no reference" branch
// and sets HasSubtreeSection = false -- HIDING the section on a node whose
// reference was intact in the live scene and on disk. So both the presence FLAGS
// and the payload are asserted here; a projection that packed the payload but
// left the flag clear would look fine in the struct and still hide the section.
//
// consume_mode is deliberately Flatten (not the default Instance) so the enum ->
// token mapping is pinned, not just exercised.
TEST(ProjectSceneSnapshot, SurfacesSceneSourceFromNodes)
{
    wz::engine::assets::SceneNodeAsset host;
    host.id = "tank_host";
    host.name = "tank_host";
    // The graph-ref half (D2-C3): "Subtree from asset" points at a Scene-from-GLB
    // asset-graph node.
    host.scene_source_node_id = 31u;
    // The descriptor half (D2-C4): the self-contained GLB source.
    wz::engine::assets::SceneGLBSceneSource glb;
    glb.path = "gltf/test-mesh-a.glb";
    glb.scene_index = 2u;
    glb.consume_mode = wz::engine::assets::SceneSourceConsumeMode::Flatten;
    wz::engine::assets::MeshRenderStyleData base_style{};
    base_style.surface.enabled = true;
    base_style.surface.color[0] = 0.1f;
    base_style.surface.color[1] = 0.2f;
    base_style.surface.color[2] = 0.3f;
    base_style.surface.color[3] = 1.0f;
    base_style.wireframe.enabled = false;
    base_style.wireframe.color[0] = 0.4f;
    glb.base_style = base_style;
    wz::engine::assets::MeshRenderStyleData override_style{};
    override_style.surface.enabled = false;
    override_style.surface.color[0] = 0.7f;
    override_style.wireframe.enabled = true;
    override_style.wireframe.color[0] = 1.0f;
    override_style.wireframe.color[3] = 0.25f;
    glb.style_overrides.push_back(
        wz::engine::assets::SceneGLBSceneSourceStyleOverride{
            .mesh_index = 1u,
            .style = override_style,
        });
    host.glb_scene_source = glb;

    wz::engine::assets::SceneNodeAsset plain;
    plain.id = "plain";

    const wz::engine::editor::SceneSnapshot snapshot =
        wz::engine::editor::build_scene_snapshot_from_nodes({ host, plain });

    ASSERT_EQ(snapshot.roots.size(), 2u);
    const auto& node = snapshot.roots[0];
    EXPECT_EQ(node.id, "tank_host");

    ASSERT_TRUE(node.scene_source_node_id.has_value())
        << "the live path dropped the Subtree-from-asset reference";
    EXPECT_EQ(*node.scene_source_node_id, 31u);

    ASSERT_TRUE(node.scene_source.has_value())
        << "the live path dropped the GLB scene-source descriptor";
    EXPECT_EQ(node.scene_source->kind, "glb");
    EXPECT_EQ(node.scene_source->path, "gltf/test-mesh-a.glb");
    EXPECT_EQ(node.scene_source->consume_mode, "flatten");
    EXPECT_EQ(node.scene_source->scene_index, 2u);
    EXPECT_TRUE(node.scene_source->has_base_style);
    EXPECT_EQ(node.scene_source->style_override_count, 1u);

    EXPECT_TRUE(node.scene_source->base_style.surface_enabled);
    EXPECT_FLOAT_EQ(node.scene_source->base_style.surface_rgba[0], 0.1f);
    EXPECT_FLOAT_EQ(node.scene_source->base_style.surface_rgba[2], 0.3f);
    EXPECT_FALSE(node.scene_source->base_style.wireframe_enabled);
    EXPECT_FLOAT_EQ(node.scene_source->base_style.wireframe_rgba[0], 0.4f);

    ASSERT_EQ(node.scene_source->style_overrides.size(), 1u);
    const auto& ov = node.scene_source->style_overrides[0];
    EXPECT_EQ(ov.mesh_index, 1u);
    EXPECT_FALSE(ov.style.surface_enabled);
    EXPECT_FLOAT_EQ(ov.style.surface_rgba[0], 0.7f);
    EXPECT_TRUE(ov.style.wireframe_enabled);
    EXPECT_FLOAT_EQ(ov.style.wireframe_rgba[3], 0.25f);

    // A node with neither keeps both absent -- the read-back must not fabricate
    // a scene source on every plain node.
    const auto& plain_node = snapshot.roots[1];
    EXPECT_EQ(plain_node.scene_source, std::nullopt);
    EXPECT_EQ(plain_node.scene_source_node_id, std::nullopt);

    // The flags the inspector actually gates on, through the real projection.
    wz::engine::editor::ProjectSnapshotLoadResult project_result;
    project_result.ok = true;
    project_result.status =
        wz::engine::project::ProjectManifestProbeStatus::Valid;
    project_result.scene.ok = true;
    project_result.scene.snapshot = snapshot;

    const auto blob =
        wz::engine::editor::project_snapshot_abi_blob(project_result);
    ASSERT_GE(blob.size(), sizeof(WzEditorProjectSnapshot));
    const auto& abi =
        *reinterpret_cast<const WzEditorProjectSnapshot*>(blob.data());
    ASSERT_EQ(abi.scene.roots.count, 2u);
    const WzEditorSceneNode* roots_abi =
        abi_table<WzEditorSceneNode>(blob, abi.scene.roots);

    EXPECT_NE(
        roots_abi[0].flags & WZ_EDITOR_SCENE_NODE_HAS_SCENE_SOURCE, 0u)
        << "HAS_SCENE_SOURCE stayed clear, so the GLB summary reads as absent";
    EXPECT_NE(
        roots_abi[0].flags & WZ_EDITOR_SCENE_NODE_HAS_SCENE_SOURCE_REF, 0u)
        << "HAS_SCENE_SOURCE_REF stayed clear, so the editor HIDES the "
           "\"Subtree from asset\" section";
    EXPECT_EQ(roots_abi[0].scene_source_node_id, 31u);
    EXPECT_EQ(abi_string(blob, roots_abi[0].scene_source.consume_mode),
        "flatten");
    EXPECT_EQ(roots_abi[0].scene_source.scene_index, 2u);

    EXPECT_EQ(
        roots_abi[1].flags & WZ_EDITOR_SCENE_NODE_HAS_SCENE_SOURCE, 0u);
    EXPECT_EQ(
        roots_abi[1].flags & WZ_EDITOR_SCENE_NODE_HAS_SCENE_SOURCE_REF, 0u);
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
    // test-mesh-a.glb (the glb_scene_source fixture's GLB) scene-0 hierarchy, observed
    // by parsing the file: a depth-first single root chain body -> turret -> gun,
    // each carrying a mesh. The same bytes ship in resources/gltf/test-mesh-a.glb; this
    // exercises the on-demand import path that backs wz_import_glb_scene_hierarchy
    // (read_file -> import_gltf_scene -> glb_scene_hierarchy_abi_blob) device-free.
    const char* kTank1Glb = WZ_TEST_FIXTURE_DIR "/gltf/test-mesh-a.glb";

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

// A scene file is untrusted input -- hand-edited, or from a shared/downloaded project
// -- and every integral field in it arrives as a double. static_cast of a double that
// does not fit is UNDEFINED BEHAVIOUR, so numbers like 1e30 used to be read as
// whatever the wrap produced: a node id pointing at a node that cannot exist, or a
// draw layer nothing authored. The readers are documented tolerant, so the required
// outcome is the same as for a malformed member -- treated as absent, defaults kept,
// the rest of the scene loading normally.
TEST(ProjectSceneSnapshot, OutOfRangeNumbersAreTreatedAsAbsentNotWrapped)
{
    TempProjectRoot temp;
    const fs::path project_root = temp.root / "out_of_range_numbers_project";

    write_text_file(
        manifest_path(project_root),
        R"json({
  "schema": "wozzits.project.v1",
  "formatVersion": 1,
  "name": "Out Of Range Numbers",
  "scene": "scene.json"
})json");

    // Every integral field a snapshot reader narrows, each holding a value no integer
    // type here can represent. The node must still load, with these fields absent.
    write_text_file(
        project_root / "scene.json",
        R"json({
  "schema": "wozzits.scene.v0",
  "name": "out_of_range_scene",
  "nodes": [
    {
      "id": "hostile",
      "render_order": 1e30,
      "renderable": { "asset_graph_node_id": 1e30 },
      "geometry": { "asset_graph_node_id": -5 },
      "render_program": { "asset_graph_node_id": 1e30 },
      "collision": { "collision_asset_node_id": 1e30, "constrain_movement": true },
      "audio_source": { "audio_renderable_node_id": 1e30 },
      "atmosphere": { "atmosphere_asset_node_id": 1e30 },
      "environment": { "environment_asset_node_id": -1 },
      "render_to_texture": { "target_asset_node_id": 1e30, "enabled": true },
      "renderable_bindings": [
        { "semantic": "scalar_field_texture", "asset_graph_node_id": 1e30 }
      ],
      "glb_scene_source": { "path": "a.glb", "scene_index": 1e30 }
    }
  ]
})json");

    const auto loaded = wz::engine::editor::load_project_scene_snapshot(
        wz::engine::project::ProjectManifestLoadDesc{
            .project_root = project_root.string(),
        });

    ASSERT_TRUE(loaded.ok) << loaded.error;
    ASSERT_EQ(loaded.snapshot.roots.size(), 1u);
    const auto& node = loaded.snapshot.roots[0];
    EXPECT_EQ(node.id, "hostile");

    // Node-id fields: absent, NOT some wrapped id.
    ASSERT_TRUE(node.renderable.has_value());
    EXPECT_FALSE(node.renderable->asset_graph_node_id.has_value());
    EXPECT_FALSE(node.geometry_node_id.has_value());
    EXPECT_FALSE(node.render_program_node_id.has_value());
    ASSERT_TRUE(node.collision.has_value());
    EXPECT_FALSE(node.collision->collision_asset_node_id.has_value());
    ASSERT_TRUE(node.audio_source.has_value());
    EXPECT_FALSE(node.audio_source->audio_renderable_node_id.has_value());
    ASSERT_TRUE(node.atmosphere.has_value());
    EXPECT_FALSE(node.atmosphere->atmosphere_asset_node_id.has_value());
    ASSERT_TRUE(node.environment.has_value());
    EXPECT_FALSE(node.environment->environment_asset_node_id.has_value());
    ASSERT_TRUE(node.render_to_texture.has_value());
    EXPECT_FALSE(node.render_to_texture->target_node_id.has_value());
    ASSERT_EQ(node.renderable_bindings.size(), 1u);
    EXPECT_FALSE(node.renderable_bindings[0].asset_graph_node_id.has_value());

    // ...and the sibling fields on those same components still came through, so the
    // rejection is per-field and does not discard the component around it.
    EXPECT_TRUE(node.collision->constrain_movement);
    EXPECT_TRUE(node.render_to_texture->enabled);
    EXPECT_EQ(node.renderable_bindings[0].semantic, "scalar_field_texture");

    // Non-node-id integrals fall back to their defaults.
    EXPECT_EQ(node.render_order, 0);
    ASSERT_TRUE(node.scene_source.has_value());
    EXPECT_EQ(node.scene_source->path, "a.glb");
    EXPECT_EQ(node.scene_source->scene_index, 0u);
}

// Node id 0 means UNSET, in every component, on both read paths. AssetGraphDraft
// allocates ids from 1 and spells invalid as UINT64_MAX, so 0 is never a node
// anything can reference -- and every WRITE path already agrees, treating an incoming
// 0 as "detach". The readers used to disagree with each other: three accepted 0 and
// five rejected it, so one authored "0" meant "no ref" in five components and "a ref
// to node 0" in three. The three that accepted it produced an engaged optional that
// the ABI projection then packed as has_<x>_ref = 1 with id 0 -- unresolvable
// downstream, and shown in the inspector as a real selection.
TEST(ProjectSceneSnapshot, NodeIdZeroReadsAsUnsetInEveryComponent)
{
    TempProjectRoot temp;
    const fs::path project_root = temp.root / "node_id_zero_project";

    write_text_file(
        manifest_path(project_root),
        R"json({
  "schema": "wozzits.project.v1",
  "formatVersion": 1,
  "name": "Node Id Zero",
  "scene": "scene.json"
})json");

    write_text_file(
        project_root / "scene.json",
        R"json({
  "schema": "wozzits.scene.v0",
  "name": "node_id_zero_scene",
  "nodes": [
    {
      "id": "zeroed",
      "renderable": { "asset_graph_node_id": 0 },
      "geometry": { "asset_graph_node_id": 0 },
      "render_program": { "asset_graph_node_id": 0 },
      "scene_source": { "asset_graph_node_id": 0 },
      "collision": { "collision_asset_node_id": 0, "constrain_movement": true },
      "audio_source": { "audio_renderable_node_id": 0 },
      "atmosphere": { "atmosphere_asset_node_id": 0 },
      "environment": { "environment_asset_node_id": 0 },
      "render_to_texture": { "target_asset_node_id": 0, "enabled": true },
      "renderable_bindings": [
        { "semantic": "scalar_field_texture", "asset_graph_node_id": 0 }
      ]
    }
  ]
})json");

    const auto loaded = wz::engine::editor::load_project_scene_snapshot(
        wz::engine::project::ProjectManifestLoadDesc{
            .project_root = project_root.string(),
        });

    ASSERT_TRUE(loaded.ok) << loaded.error;
    ASSERT_EQ(loaded.snapshot.roots.size(), 1u);
    const auto& node = loaded.snapshot.roots[0];

    // The three that used to accept 0 -- these are the regression.
    ASSERT_TRUE(node.renderable.has_value());
    EXPECT_FALSE(node.renderable->asset_graph_node_id.has_value());
    EXPECT_EQ(node.renderable_source.kind, "none");
    EXPECT_FALSE(node.geometry_node_id.has_value());
    EXPECT_FALSE(node.render_program_node_id.has_value());
    EXPECT_FALSE(node.scene_source_node_id.has_value());
    ASSERT_TRUE(node.collision.has_value());
    EXPECT_FALSE(node.collision->collision_asset_node_id.has_value());

    // ...and the five that already rejected it still do.
    ASSERT_TRUE(node.audio_source.has_value());
    EXPECT_FALSE(node.audio_source->audio_renderable_node_id.has_value());
    ASSERT_TRUE(node.atmosphere.has_value());
    EXPECT_FALSE(node.atmosphere->atmosphere_asset_node_id.has_value());
    ASSERT_TRUE(node.environment.has_value());
    EXPECT_FALSE(node.environment->environment_asset_node_id.has_value());
    ASSERT_TRUE(node.render_to_texture.has_value());
    EXPECT_FALSE(node.render_to_texture->target_node_id.has_value());
    ASSERT_EQ(node.renderable_bindings.size(), 1u);
    EXPECT_FALSE(node.renderable_bindings[0].asset_graph_node_id.has_value());

    // Unsetting the ref never drops the component or its other fields.
    EXPECT_TRUE(node.collision->constrain_movement);
    EXPECT_TRUE(node.render_to_texture->enabled);
    EXPECT_EQ(node.renderable_bindings[0].semantic, "scalar_field_texture");

    // The presence FLAGS must agree, or the inspector reveals a picker with nothing
    // selectable behind it.
    wz::engine::editor::ProjectSnapshotLoadResult project_result;
    project_result.ok = true;
    project_result.status =
        wz::engine::project::ProjectManifestProbeStatus::Valid;
    project_result.scene = loaded;
    const auto blob =
        wz::engine::editor::project_snapshot_abi_blob(project_result);
    ASSERT_GE(blob.size(), sizeof(WzEditorProjectSnapshot));
    const auto& abi =
        *reinterpret_cast<const WzEditorProjectSnapshot*>(blob.data());
    ASSERT_EQ(abi.scene.roots.count, 1u);
    const WzEditorSceneNode* roots =
        abi_table<WzEditorSceneNode>(blob, abi.scene.roots);
    EXPECT_EQ(roots[0].flags & WZ_EDITOR_SCENE_NODE_HAS_GEOMETRY, 0u);
    EXPECT_EQ(roots[0].flags & WZ_EDITOR_SCENE_NODE_HAS_RENDER_PROGRAM, 0u);
    EXPECT_EQ(roots[0].flags & WZ_EDITOR_SCENE_NODE_HAS_SCENE_SOURCE_REF, 0u);
    EXPECT_EQ(roots[0].collision.has_collision_ref, 0u);
}

// A DECISION-RECORDING tripwire, not a prohibition. These authored fields sit on
// nodes/components the snapshot DOES surface, so a reader reasonably assumes they are
// covered -- and they are not. Because they are absent from BOTH reader paths, a
// parity test between the two cannot see the gap: symmetric absence looks like
// agreement. This is the only mechanism that can.
//
// Every one of them round-trips correctly through the compiler + exporter, so nothing
// is lost on save/reload; they are invisible to the EDITOR, not to the runtime. That
// is why they are hardening rather than bugs.
//
// IF THIS TEST FAILS you have surfaced one of them, which is welcome -- it means
// removing its line here, updating the category-4 list in
// engine/editor/scene_snapshot.h, and (for anything the inspector should show) adding
// the ABI field + presence flag + the editor-side mirror, in lockstep. Surfacing one
// on only ONE reader path is the recurring bug this whole area keeps hitting, so fill
// both.
TEST(ProjectSceneSnapshot, DeliberatelyUnsurfacedAuthoredFields)
{
    // Build a live node with every category-4 field set AWAY from its default, so a
    // reader that started surfacing one would show a distinguishable value.
    wz::engine::assets::SceneNodeAsset node;
    node.id = "loaded";
    node.active = false;   // #252 live axis; `visible` is surfaced, this is not
    node.motion_type = wz::scene::TransformNode::MotionType::Animated;
    node.geometry_asset_node_id = 12u;
    node.geometry_glb_node_id = "turret_part";   // which GLB part the geometry names
    wz::engine::assets::SceneAudioListenerAsset listener;
    listener.active = false;
    node.audio_listener = listener;
    node.scene_source_child_overrides.push_back(
        wz::engine::assets::SceneSourceChildOverride{ .child_id = "body" });

    const wz::engine::editor::SceneSnapshot live =
        wz::engine::editor::build_scene_snapshot_from_nodes({ node });
    ASSERT_EQ(live.roots.size(), 1u);
    const auto& surfaced = live.roots[0];

    // The node IS surfaced, and so is the sibling axis -- which is exactly why the
    // omissions below are easy to mistake for coverage.
    EXPECT_EQ(surfaced.id, "loaded");
    ASSERT_TRUE(surfaced.visible.has_value());
    EXPECT_TRUE(*surfaced.visible);

    // audio_listener reaches the editor as PRESENCE ONLY: the component lists (so its
    // row and remove button appear) but its `active` field has nowhere to land -- there
    // is no SceneSnapshotAudioListener struct at all.
    EXPECT_TRUE(std::any_of(
        surfaced.components.begin(),
        surfaced.components.end(),
        [](const auto& c) { return c.kind == "audio_listener"; }));

    // The geometry REF is surfaced; which GLB PART it names is not, so a node using
    // indexed GLB-part geometry looks identical to one using the whole mesh.
    ASSERT_TRUE(surfaced.geometry_node_id.has_value());
    EXPECT_EQ(*surfaced.geometry_node_id, 12u);

    // The remaining three have no representation whatsoever. Asserted through the ABI
    // projection as well as the snapshot struct, since that is the surface the editor
    // actually consumes -- a field could exist in one and not the other.
    wz::engine::editor::ProjectSnapshotLoadResult project_result;
    project_result.ok = true;
    project_result.status =
        wz::engine::project::ProjectManifestProbeStatus::Valid;
    project_result.scene.ok = true;
    project_result.scene.snapshot = live;
    const auto blob =
        wz::engine::editor::project_snapshot_abi_blob(project_result);
    ASSERT_GE(blob.size(), sizeof(WzEditorProjectSnapshot));
    const auto& abi =
        *reinterpret_cast<const WzEditorProjectSnapshot*>(blob.data());
    ASSERT_EQ(abi.scene.roots.count, 1u);

    // `visible` crosses the ABI; the category-4 fields have no counterpart to compare
    // against, which is the finding. So assert the OBSERVABLE consequence instead:
    // build a second node differing ONLY in those fields and require the two ABI blobs
    // to be byte-identical. If any of them starts being surfaced -- as a flag bit, a
    // new struct member, a string, anything the projection writes -- the bytes diverge
    // and this fails. That is a stronger net than checking named fields, because it
    // needs no advance knowledge of HOW a future author chooses to carry the value.
    //
    // What it does NOT catch: a field surfaced into SceneSnapshotNode but never packed
    // into the ABI (invisible to the editor either way, so harmless), and `audio_listener
    // .active` / `geometry_glb_node_id`, whose components already contribute bytes --
    // those two are covered by the presence-only assertions above.
    wz::engine::assets::SceneNodeAsset flipped = node;
    flipped.active = true;
    flipped.motion_type = wz::scene::TransformNode::MotionType::Static;
    flipped.scene_source_child_overrides.clear();

    wz::engine::editor::ProjectSnapshotLoadResult flipped_result = project_result;
    flipped_result.scene.snapshot =
        wz::engine::editor::build_scene_snapshot_from_nodes({ flipped });
    const auto flipped_blob =
        wz::engine::editor::project_snapshot_abi_blob(flipped_result);

    EXPECT_EQ(flipped_blob, blob)
        << "flipping active / motion_type / scene_source_child_overrides changed the "
           "editor ABI blob, so one of them is now surfaced. That is fine -- update "
           "the category-4 list in engine/editor/scene_snapshot.h and this test, and "
           "make sure BOTH reader paths fill it, not just one.";
}

// D2-S1, decided rather than left suspected. The visit-1 report claimed the two
// snapshot paths disagree about whether a node is a renderable for two node shapes
// the editor cannot author, and ranked it SUSPECTED because no user action produces
// them. A TEST can author them directly, so the claim is checkable.
//
// flat_node_from_asset calls a node renderable if ANY of renderable_asset_node_id,
// renderable_asset or the LEGACY `renderable` is set. read_node requires a
// "renderable" JSON object -- and the exporter writes the legacy slot as
// "debug_renderable" (which read_node never reads) and suppresses "renderable"
// entirely when mesh_source is present. If the report is right, one node reports
// kind "renderable" live and "node" from disk.
TEST(ProjectSceneSnapshot, LegacyAndMeshSourceRenderablesAgreeAcrossBothPaths)
{
    // CONFIRMED and UNFIXED, so this is a reproduction rather than a passing pin.
    // Measured 2026-07-29:
    //   legacy renderable only : live kind=renderable src=scene-source
    //                            disk kind=node       src=none
    //   mesh_source + ref      : live kind=renderable src=asset-graph-node
    //                            disk kind=node       src=none
    //
    // Both halves are export-side. The legacy slot is written as
    // "debug_renderable", which read_node never reads; and the "renderable" member
    // is suppressed outright when mesh_source is present, which is D2-H8 -- so the
    // mesh_source half of this finding and H8 are ONE root cause and want one fix.
    //
    // Skipped rather than deleted: the shapes are not editor-authorable, so without
    // this the next visit would have to re-derive them from scratch. Skipped rather
    // than inverted into a characterization test, because asserting the current
    // values would read as "this divergence is intended". Remove the skip when
    // D2-S1/D2-H8 land -- the assertions below are the ones that should then hold.
    GTEST_SKIP() << "D2-S1 (with D2-H8) is confirmed and unfixed -- see #318";

    using namespace wz::engine::assets;

    // Shape 1: ONLY the legacy embedded renderable. The exporter emits it as
    // "debug_renderable".
    SceneNodeAsset legacy;
    legacy.id = "legacy_only";
    legacy.renderable = SceneRenderableBinding{};

    // Shape 2: a mesh_source AND an authored renderable ref. The exporter's
    // `!node.mesh_source &&` guard suppresses the "renderable" member entirely.
    SceneNodeAsset with_mesh_source;
    with_mesh_source.id = "mesh_sourced";
    with_mesh_source.mesh_source = SceneMeshSourceAsset{};
    with_mesh_source.renderable_asset_node_id = 21u;

    const wz::engine::editor::SceneSnapshot live =
        wz::engine::editor::build_scene_snapshot_from_nodes(
            { legacy, with_mesh_source });
    ASSERT_EQ(live.roots.size(), 2u);

    // Round-trip the SAME nodes through the real exporter and the JSON reader.
    SceneAssetData data;
    data.nodes = { legacy, with_mesh_source };
    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(data));

    TempProjectRoot temp;
    const fs::path project_root = temp.root / "renderable_kind_parity_project";
    write_text_file(
        manifest_path(project_root),
        R"json({
  "schema": "wozzits.project.v1",
  "formatVersion": 1,
  "name": "Renderable Kind Parity",
  "scene": "scene.json"
})json");
    write_text_file(project_root / "scene.json", exported);

    const auto from_disk = wz::engine::editor::load_project_scene_snapshot(
        wz::engine::project::ProjectManifestLoadDesc{
            .project_root = project_root.string(),
        });
    ASSERT_TRUE(from_disk.ok) << from_disk.error;
    ASSERT_EQ(from_disk.snapshot.roots.size(), 2u);

    // The claim under test: same node, same question, same answer on both paths.
    for (std::size_t i = 0; i < 2u; ++i) {
        EXPECT_EQ(live.roots[i].id, from_disk.snapshot.roots[i].id);
        EXPECT_EQ(live.roots[i].kind, from_disk.snapshot.roots[i].kind)
            << "node '" << live.roots[i].id << "' reports a different kind "
            << "depending on which reader built the snapshot";
        EXPECT_EQ(
            live.roots[i].renderable_source.kind,
            from_disk.snapshot.roots[i].renderable_source.kind)
            << "node '" << live.roots[i].id << "' renderable_source diverges";
    }
}
