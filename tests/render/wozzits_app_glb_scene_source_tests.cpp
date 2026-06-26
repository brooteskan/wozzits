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
