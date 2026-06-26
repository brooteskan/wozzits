// tests/render/wozzits_app_glb_scene_source_tests.cpp
//
// On-device coverage for the issue #213 GLB scene-source DESCRIPTOR route in
// WozzitsApp_v1. A host scene node carries a glb_scene_source descriptor (a
// resource-relative GLB path + scene_index + consume_mode + per-component style
// mapping) instead of an asset-graph node reference. At scene materialization
// WozzitsApp_v1::resolve_glb_scene_sources registers the GLB + calls
// create_scene_from_glb and writes the resolved Scene key into the node's
// scene_source; the existing graft (instance) or flatten then consumes it.
//
// The descriptor is NOT part of the asset-graph draft, so it must be re-resolved
// on every load + rebind to survive bind's wholesale replace_registered_assets.
// These tests prove the load path end-to-end on a real app:
//   - instance mode grafts the GLB hierarchy as host-namespaced children that a
//     behavior can address (the headline workflow), and the host keeps its live
//     descriptor;
//   - flatten mode expands the children persistently and DROPS the descriptor.
// The per-component "distinct styles" assertion lives in the device-free
// asset_scene/scene_source_expansion tests, where the resolved renderable keys
// are observable; here the GLB import path is exercised with styles present.
//
// On-device (WozzitsApp_v1 owns a RhiSceneRenderer needing a DX12 device).
// Skipped if no device can be created. The fixture (glb_scene_source_fixture)
// reuses the behavior fixture's staged module dir so "move_up_on_frame" is
// available to bind to a grafted child.

#include <gtest/gtest.h>

#include <engine/app/wozzits_app_v1.h>
#include <engine/app_context.h>
#include <engine/project/project_manifest.h>

#include <gpu/gpu.h>
#include <input/input.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    constexpr const char* kProjectRoot = "projects/glb_scene_source_fixture";

    struct WozzitsAppGlbSceneSourceFixture : public ::testing::Test
    {
        wz::engine::AppContext ctx;
        bool initialized = false;

        void SetUp() override
        {
            wz::engine::AppDesc desc;
            desc.window = {
                "wozzits_app_v1_glb_scene_source_test", 256, 256, false, false };
            desc.resource_root = "resources";
            initialized = wz::engine::init(ctx, desc);
            if (!initialized) {
                GTEST_SKIP()
                    << "no GPU device — skipping on-device GLB scene-source test";
            }
        }

        void TearDown() override
        {
            if (initialized) {
                wz::engine::shutdown(ctx);
            }
        }

        wz::engine::project::ProjectManifestLoadResult load_test_project() const
        {
            return wz::engine::project::load_project_manifest(
                wz::engine::project::ProjectManifestLoadDesc{
                    .project_root = kProjectRoot,
                    .resource_root = ctx.assets->resource_root(),
                });
        }
    };
}

// (a) Instance mode: a host node carrying a glb_scene_source descriptor loads,
// resolves the GLB into a Scene, and grafts its hierarchy as host-namespaced
// instanced children — addressable by a behavior. The live descriptor persists.
TEST_F(WozzitsAppGlbSceneSourceFixture, InstanceGraftsGlbHierarchyAndIsAddressable)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;

    wz::app::WozzitsAppSceneLoadDesc load_desc{};
    load_desc.asset_graph = project.manifest.asset_graph_path;
    load_desc.scene = project.manifest.scene_path;
    load_desc.behavior_module_folder = project.manifest.behavior_module_folder;

    // The empty asset graph compiles fine and the GLB scene resolves, so
    // load_scene returns true overall.
    EXPECT_TRUE(app.load_scene(load_desc));

    // The GLB hierarchy (tank1.glb: body -> turret -> gun) is grafted under the
    // host with namespaced ids, sub-scene parenting preserved (roots reparent to
    // the host). Presence is observed via node_local_translation (nullopt if the
    // node is absent from scene_nodes_, the renderer's source of truth).
    EXPECT_TRUE(app.node_local_translation("tank_host/body").has_value())
        << "instance graft did not add the GLB body child";
    EXPECT_TRUE(app.node_local_translation("tank_host/turret").has_value())
        << "instance graft did not add the GLB turret child";
    EXPECT_TRUE(app.node_local_translation("tank_host/gun").has_value())
        << "instance graft did not add the GLB gun child";

    // Direct child of the host is the sub-scene root (body); turret/gun nest
    // deeper, so the host has exactly one direct child.
    EXPECT_EQ(app.child_node_count("tank_host"), 1u)
        << "the sub-scene root should reparent to the host as its sole direct "
           "child";
    EXPECT_EQ(app.child_node_count("tank_host/body"), 1u)
        << "turret should remain parented under body in the host namespace";

    // Instance mode keeps the live descriptor (the link persists across rebinds,
    // unlike flatten which bakes + drops it).
    EXPECT_TRUE(app.node_has_glb_scene_source("tank_host"))
        << "instance mode must keep the live glb_scene_source descriptor";

    // A behavior can ADDRESS a grafted child by its namespaced id: binding the
    // move-up module to the grafted turret materializes a live binding (proving
    // the grafted child entered the behavior runtime's authored set), and a tick
    // moves only that child.
    const std::optional<wz::math::Vec3> before =
        app.node_local_translation("tank_host/turret");
    ASSERT_TRUE(before.has_value());

    const wz::engine::assets::SceneAddBehaviorResult added =
        app.add_node_behavior("tank_host/turret", "move_up_on_frame");
    ASSERT_TRUE(added.ok) << added.error;
    EXPECT_EQ(app.active_behavior_binding_count(), 1u)
        << "the behavior bound to the grafted child was not materialized";

    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    const std::optional<wz::math::Vec3> after =
        app.node_local_translation("tank_host/turret");
    ASSERT_TRUE(after.has_value());
    EXPECT_FLOAT_EQ(after->y, before->y + 1.0f)
        << "a behavior could not drive the grafted GLB child by namespaced id";
}

// (b) Flatten mode: the host's glb_scene_source resolves and expands its GLB
// hierarchy persistently into real authored children, then the descriptor is
// DROPPED (the expansion is now the content — a one-time bake, no live link).
TEST_F(WozzitsAppGlbSceneSourceFixture, FlattenExpandsPersistentlyAndDropsDescriptor)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;

    // Same fixture, the flatten-mode scene variant.
    wz::app::WozzitsAppSceneLoadDesc load_desc{};
    load_desc.asset_graph = project.manifest.asset_graph_path;
    load_desc.scene =
        wz::fs::Path(std::string(kProjectRoot) + "/scene_flatten.json");
    load_desc.behavior_module_folder = project.manifest.behavior_module_folder;

    EXPECT_TRUE(app.load_scene(load_desc));

    // The GLB hierarchy is expanded the same way (host-namespaced children,
    // parenting preserved) — present in scene_nodes_.
    EXPECT_TRUE(app.node_local_translation("tank_host/body").has_value())
        << "flatten did not expand the GLB body child";
    EXPECT_TRUE(app.node_local_translation("tank_host/turret").has_value())
        << "flatten did not expand the GLB turret child";
    EXPECT_TRUE(app.node_local_translation("tank_host/gun").has_value())
        << "flatten did not expand the GLB gun child";
    EXPECT_EQ(app.child_node_count("tank_host"), 1u);
    EXPECT_EQ(app.child_node_count("tank_host/body"), 1u);

    // Flatten BAKES: the live descriptor is dropped (no scene_source link kept),
    // so the children are now fully-authored, editable nodes — the distinguishing
    // contrast with instance mode (which keeps the descriptor).
    EXPECT_FALSE(app.node_has_glb_scene_source("tank_host"))
        << "flatten must drop the glb_scene_source descriptor after baking";
    EXPECT_FALSE(app.node_scene_source_node_id("tank_host").has_value());

    // The flattened children are addressable too (they are real authored nodes).
    // The child keeps its GLB-authored local transform, so assert the behavior's
    // +1 delta rather than an absolute value.
    const std::optional<wz::math::Vec3> before =
        app.node_local_translation("tank_host/turret");
    ASSERT_TRUE(before.has_value());
    const wz::engine::assets::SceneAddBehaviorResult added =
        app.add_node_behavior("tank_host/turret", "move_up_on_frame");
    ASSERT_TRUE(added.ok) << added.error;
    EXPECT_EQ(app.active_behavior_binding_count(), 1u);
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    const std::optional<wz::math::Vec3> after =
        app.node_local_translation("tank_host/turret");
    ASSERT_TRUE(after.has_value());
    EXPECT_FLOAT_EQ(after->y, before->y + 1.0f)
        << "a behavior could not drive a flattened GLB child";
}

// (c) Live authoring (issue #213 Phase 3a): the editor route. A plain host node
// with no scene source gets a GLB scene-source DESCRIPTOR authored onto it via
// WozzitsApp_v1::set_node_glb_scene_source — the apply behind the host-ABI verb
// wz_host_runtime_set_node_glb_scene_source. The descriptor must resolve + graft
// the GLB hierarchy under the host on the spot (no reload), and clearing it
// (empty path) must drop both the descriptor and the grafted children. Guard:
// authoring on a missing node fails closed.
TEST_F(WozzitsAppGlbSceneSourceFixture, SetGlbSceneSourceAuthorsAndClearsLive)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;

    wz::app::WozzitsAppSceneLoadDesc load_desc{};
    load_desc.asset_graph = project.manifest.asset_graph_path;
    load_desc.scene = project.manifest.scene_path;
    load_desc.behavior_module_folder = project.manifest.behavior_module_folder;
    ASSERT_TRUE(app.load_scene(load_desc));

    // The plain "root" node carries no scene source out of the fixture.
    EXPECT_FALSE(app.node_has_glb_scene_source("root"));
    EXPECT_EQ(app.child_node_count("root/body"), 0u);

    // Guard: authoring on a node that does not exist is a fail-closed no-op.
    EXPECT_FALSE(app.set_node_glb_scene_source(
        "does_not_exist",
        wz::engine::assets::SceneGLBSceneSource{ .path = "gltf/tank1.glb" }));

    // Author a single-default-style descriptor (no base_style/style_overrides —
    // Phase 3a) onto "root": it resolves the GLB into a Scene and grafts the
    // tank1 hierarchy (body -> turret -> gun) as root-namespaced children on the
    // spot, with sub-scene parenting preserved.
    EXPECT_TRUE(app.set_node_glb_scene_source(
        "root",
        wz::engine::assets::SceneGLBSceneSource{
            .path = "gltf/tank1.glb",
            .scene_index = 0,
            .consume_mode =
                wz::engine::assets::SceneSourceConsumeMode::Instance,
        }));

    EXPECT_TRUE(app.node_has_glb_scene_source("root"))
        << "the authored descriptor was not recorded on the host";
    EXPECT_TRUE(app.node_local_translation("root/body").has_value())
        << "set_node_glb_scene_source did not graft the GLB body child";
    EXPECT_TRUE(app.node_local_translation("root/turret").has_value())
        << "set_node_glb_scene_source did not graft the GLB turret child";
    EXPECT_TRUE(app.node_local_translation("root/gun").has_value())
        << "set_node_glb_scene_source did not graft the GLB gun child";
    // body nests under root and is itself a host for turret (parenting preserved).
    EXPECT_EQ(app.child_node_count("root/body"), 1u)
        << "turret should remain parented under body in the host namespace";

    // A grafted child is behavior-addressable by its namespaced id (the graft
    // rebuilt the behavior runtime), proving it is a live scene entity.
    const wz::engine::assets::SceneAddBehaviorResult added =
        app.add_node_behavior("root/turret", "move_up_on_frame");
    ASSERT_TRUE(added.ok) << added.error;
    EXPECT_EQ(app.active_behavior_binding_count(), 1u);

    // Clearing with an empty path drops the descriptor AND the grafted children
    // (the host no longer resolves to a Scene, so graft removes the stale graft).
    EXPECT_TRUE(app.set_node_glb_scene_source(
        "root", wz::engine::assets::SceneGLBSceneSource{ .path = "" }));
    EXPECT_FALSE(app.node_has_glb_scene_source("root"))
        << "clearing must drop the glb_scene_source descriptor";
    EXPECT_FALSE(app.node_local_translation("root/body").has_value())
        << "clearing must remove the grafted GLB children";
    EXPECT_FALSE(app.node_local_translation("root/turret").has_value());
    EXPECT_FALSE(app.node_local_translation("root/gun").has_value());
    // The behavior bound to the now-removed child is gone with it.
    EXPECT_EQ(app.active_behavior_binding_count(), 0u)
        << "removing the grafted child must drop its behavior binding";

    // The fixture's OTHER descriptor node (tank_host) must be UNAFFECTED by
    // authoring/clearing root's descriptor: set_node_glb_scene_source re-resolves
    // every descriptor (no wholesale rebind wipe), so tank_host's already-
    // registered Scene must re-resolve as a cache hit and keep its graft, not be
    // dropped by a duplicate-registration failure (issue #213 idempotency).
    EXPECT_TRUE(app.node_has_glb_scene_source("tank_host"));
    EXPECT_TRUE(app.node_local_translation("tank_host/body").has_value())
        << "authoring another node must not drop tank_host's grafted children";
    EXPECT_TRUE(app.node_local_translation("tank_host/turret").has_value());
    EXPECT_TRUE(app.node_local_translation("tank_host/gun").has_value());
}

// (d) Per-component styling (issue #213 Phase 3b-2): set a base style + a per-mesh
// override on a GLB scene-source host via the app methods, assert the descriptor
// reflects them and that the scene re-grafts (children still present after the
// style-driven re-key), then clear the override and assert it is removed. The
// style change folds into create_scene_from_glb's content key, so re-materialize
// rebuilds the per-mesh renderables — observed here through descriptor state +
// the graft surviving (the visual look is a render concern, not asserted here).
TEST_F(WozzitsAppGlbSceneSourceFixture, ComponentStyleAuthorsBaseOverrideAndClears)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;

    wz::app::WozzitsAppSceneLoadDesc load_desc{};
    load_desc.asset_graph = project.manifest.asset_graph_path;
    load_desc.scene = project.manifest.scene_path;
    load_desc.behavior_module_folder = project.manifest.behavior_module_folder;
    ASSERT_TRUE(app.load_scene(load_desc));

    // tank_host carries a glb_scene_source descriptor + an instance graft. The
    // fixture authors a base style + one override (mesh_index 1) already, so this
    // test works in DELTAS against that starting state (a clean-slate assumption
    // would be wrong — the fixture proves Phase 1 styling round-trips).
    ASSERT_TRUE(app.node_has_glb_scene_source("tank_host"));
    ASSERT_TRUE(app.node_local_translation("tank_host/turret").has_value());

    ASSERT_NE(app.node_glb_scene_source("tank_host"), nullptr);
    const std::size_t fixture_override_count =
        app.node_glb_scene_source("tank_host")->style_overrides.size();

    // Guard: a style edit on a node with no GLB scene source fails closed —
    // "root" exists but has no descriptor; "does_not_exist" is absent entirely.
    EXPECT_FALSE(app.set_node_glb_base_style(
        "does_not_exist",
        wz::engine::assets::MeshRenderStyleData{}));
    EXPECT_FALSE(app.set_node_glb_mesh_style(
        "root", 0u, wz::engine::assets::MeshRenderStyleData{}));

    // Author a BASE style (surface on, a distinct color) onto tank_host. The
    // override table is untouched by a base-style edit.
    wz::engine::assets::MeshRenderStyleData base{};
    base.surface.enabled = true;
    base.surface.color[0] = 0.2f;
    base.surface.color[1] = 0.4f;
    base.surface.color[2] = 0.6f;
    base.surface.color[3] = 1.0f;
    base.wireframe.enabled = false;
    EXPECT_TRUE(app.set_node_glb_base_style("tank_host", base));

    {
        const wz::engine::assets::SceneGLBSceneSource* desc =
            app.node_glb_scene_source("tank_host");
        ASSERT_NE(desc, nullptr);
        ASSERT_TRUE(desc->base_style.has_value());
        EXPECT_TRUE(desc->base_style->surface.enabled);
        EXPECT_FLOAT_EQ(desc->base_style->surface.color[1], 0.4f);
        EXPECT_EQ(desc->style_overrides.size(), fixture_override_count)
            << "a base-style edit must not touch the override table";
    }
    // The scene re-grafted after the style-driven re-key: children persist.
    EXPECT_TRUE(app.node_local_translation("tank_host/turret").has_value())
        << "the graft must survive a base-style re-materialize";

    // Author a per-mesh OVERRIDE for a NEW mesh_index (0, distinct from the
    // fixture's mesh_index 1 override): the table grows by one.
    wz::engine::assets::MeshRenderStyleData ov{};
    ov.surface.enabled = false;
    ov.wireframe.enabled = true;
    ov.wireframe.color[0] = 1.0f;
    ov.wireframe.color[1] = 0.0f;
    ov.wireframe.color[2] = 0.0f;
    ov.wireframe.color[3] = 1.0f;
    EXPECT_TRUE(app.set_node_glb_mesh_style("tank_host", 0u, ov));

    {
        const wz::engine::assets::SceneGLBSceneSource* desc =
            app.node_glb_scene_source("tank_host");
        ASSERT_NE(desc, nullptr);
        ASSERT_EQ(desc->style_overrides.size(), fixture_override_count + 1u);
        const auto it = std::find_if(
            desc->style_overrides.begin(),
            desc->style_overrides.end(),
            [](const wz::engine::assets::SceneGLBSceneSourceStyleOverride& o) {
                return o.mesh_index == 0u;
            });
        ASSERT_NE(it, desc->style_overrides.end());
        EXPECT_TRUE(it->style.wireframe.enabled);
        EXPECT_FLOAT_EQ(it->style.wireframe.color[0], 1.0f);
    }

    // Replace-or-insert: re-authoring the SAME mesh_index updates in place (no
    // duplicate entry — count is unchanged).
    ov.wireframe.color[0] = 0.5f;
    EXPECT_TRUE(app.set_node_glb_mesh_style("tank_host", 0u, ov));
    {
        const wz::engine::assets::SceneGLBSceneSource* desc =
            app.node_glb_scene_source("tank_host");
        ASSERT_NE(desc, nullptr);
        ASSERT_EQ(desc->style_overrides.size(), fixture_override_count + 1u)
            << "re-authoring the same mesh_index must replace, not append";
        const auto it = std::find_if(
            desc->style_overrides.begin(),
            desc->style_overrides.end(),
            [](const wz::engine::assets::SceneGLBSceneSourceStyleOverride& o) {
                return o.mesh_index == 0u;
            });
        ASSERT_NE(it, desc->style_overrides.end());
        EXPECT_FLOAT_EQ(it->style.wireframe.color[0], 0.5f);
    }
    EXPECT_TRUE(app.node_local_translation("tank_host/turret").has_value())
        << "the graft must survive a per-mesh-override re-materialize";

    // Clear the override we added: it is removed (back to the fixture count), and
    // the base style remains.
    EXPECT_TRUE(app.clear_node_glb_mesh_style("tank_host", 0u));
    {
        const wz::engine::assets::SceneGLBSceneSource* desc =
            app.node_glb_scene_source("tank_host");
        ASSERT_NE(desc, nullptr);
        EXPECT_EQ(desc->style_overrides.size(), fixture_override_count)
            << "clear must remove exactly the targeted per-mesh override";
        const auto it = std::find_if(
            desc->style_overrides.begin(),
            desc->style_overrides.end(),
            [](const wz::engine::assets::SceneGLBSceneSourceStyleOverride& o) {
                return o.mesh_index == 0u;
            });
        EXPECT_EQ(it, desc->style_overrides.end());
        ASSERT_TRUE(desc->base_style.has_value())
            << "clearing an override must not drop the base style";
    }

    // Clearing a mesh index with no override is a success no-op.
    EXPECT_TRUE(app.clear_node_glb_mesh_style("tank_host", 99u));
}

// (e) Grafted-nodes accessor (issue #213, the editor scene-tree merge source):
// after an instance-mode load, WozzitsApp_v1::grafted_scene_nodes() returns a
// copy of exactly the runtime-grafted children (host-namespaced body/turret/gun
// under tank_host), each carrying its parent_id so the editor can merge the
// sub-tree under its host. Authored nodes (the host itself, "root") must NOT be
// in the set.
TEST_F(WozzitsAppGlbSceneSourceFixture, GraftedSceneNodesReturnsInstanceGraft)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;

    wz::app::WozzitsAppSceneLoadDesc load_desc{};
    load_desc.asset_graph = project.manifest.asset_graph_path;
    load_desc.scene = project.manifest.scene_path;
    load_desc.behavior_module_folder = project.manifest.behavior_module_folder;
    ASSERT_TRUE(app.load_scene(load_desc));

    const std::vector<wz::engine::assets::SceneNodeAsset> grafted =
        app.grafted_scene_nodes();

    const auto has_id = [&grafted](const std::string& id) {
        return std::any_of(
            grafted.begin(),
            grafted.end(),
            [&id](const wz::engine::assets::SceneNodeAsset& n) {
                return n.id == id;
            });
    };
    const auto find_id = [&grafted](const std::string& id)
        -> const wz::engine::assets::SceneNodeAsset* {
        const auto it = std::find_if(
            grafted.begin(),
            grafted.end(),
            [&id](const wz::engine::assets::SceneNodeAsset& n) {
                return n.id == id;
            });
        return it == grafted.end() ? nullptr : &*it;
    };

    // Exactly the three grafted GLB children, nothing else.
    EXPECT_EQ(grafted.size(), 3u);
    EXPECT_TRUE(has_id("tank_host/body"));
    EXPECT_TRUE(has_id("tank_host/turret"));
    EXPECT_TRUE(has_id("tank_host/gun"));

    // The authored host + the fixture's plain "root" node are NOT grafts.
    EXPECT_FALSE(has_id("tank_host"));
    EXPECT_FALSE(has_id("root"));

    // The sub-scene root (body) reparents to the host; the deeper nodes keep
    // their host-namespaced parents — this parent_id is what the editor merges on.
    const wz::engine::assets::SceneNodeAsset* body = find_id("tank_host/body");
    ASSERT_NE(body, nullptr);
    ASSERT_TRUE(body->parent_id.has_value());
    EXPECT_EQ(*body->parent_id, "tank_host");

    const wz::engine::assets::SceneNodeAsset* turret =
        find_id("tank_host/turret");
    ASSERT_NE(turret, nullptr);
    ASSERT_TRUE(turret->parent_id.has_value());
    EXPECT_EQ(*turret->parent_id, "tank_host/body");

    // Clearing the descriptor drops the graft, so the accessor returns empty.
    EXPECT_TRUE(app.set_node_glb_scene_source(
        "tank_host", wz::engine::assets::SceneGLBSceneSource{ .path = "" }));
    EXPECT_TRUE(app.grafted_scene_nodes().empty())
        << "clearing the scene source must empty the grafted-nodes set";
}
