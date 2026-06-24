// tests/render/wozzits_app_behavior_dispatch_tests.cpp
//
// Coverage for the WozzitsApp_v1 behavior runtime: a project whose scene binds a
// node to a project behavior-module DLL must, inside the shared runtime, load +
// register that DLL, dispatch frame.update to the binding, and apply the
// produced transform command — with the result visible on the authored scene
// node (what the renderer draws from).
//
// The fixture (behavior_test_fixture) is self-contained and separate from the
// editable sample projects: an empty asset graph + a scene with one "mover" node
// bound to the module "move_up_on_frame", whose DLL (behavior_module_test_plugin)
// CMake stages into the fixture's behavior_module_folder. The module writes an
// add-local-translation of (0, +1, 0) on every frame.update.
//
// This is an on-device test (WozzitsApp_v1 owns a RhiSceneRenderer that needs a
// DX12 device). Skipped if no device can be created.

#include <gtest/gtest.h>

#include <engine/app/wozzits_app_v1.h>
#include <engine/app_context.h>
#include <engine/project/project_manifest.h>

#include <gpu/gpu.h>
#include <input/input.h>

namespace
{
    constexpr const char* kProjectRoot = "projects/behavior_test_fixture";

    struct WozzitsAppBehaviorFixture : public ::testing::Test
    {
        wz::engine::AppContext ctx;
        bool initialized = false;

        void SetUp() override
        {
            wz::engine::AppDesc desc;
            desc.window = {
                "wozzits_app_v1_behavior_test", 256, 256, false, false };
            desc.resource_root = "resources";
            initialized = wz::engine::init(ctx, desc);
            if (!initialized) {
                GTEST_SKIP()
                    << "no GPU device — skipping on-device behavior test";
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

        wz::app::WozzitsAppSceneLoadDesc scene_load_desc(
            const wz::engine::project::ProjectManifest& project) const
        {
            return wz::app::WozzitsAppSceneLoadDesc{
                .asset_graph = project.asset_graph_path,
                .scene = project.scene_path,
                .behavior_module_folder = project.behavior_module_folder,
            };
        }
    };
}

// load_scene must load the project behavior DLL, register its module, and
// materialize the scene's one behavior binding.
TEST_F(WozzitsAppBehaviorFixture, LoadSceneMaterializesProjectBehavior)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;
    ASSERT_FALSE(project.manifest.behavior_module_folder.empty())
        << "fixture manifest must point at the staged behavior module folder";

    // The empty asset graph compiles fine; load_scene returns true overall.
    EXPECT_TRUE(app.load_scene(scene_load_desc(project.manifest)));
    EXPECT_EQ(app.active_behavior_binding_count(), 1u)
        << "the scene's one behavior binding was not materialized";
}

// A simulation_tick must dispatch frame.update to the binding and apply the
// resulting add-local-translation command, moving the authored node up.
TEST_F(WozzitsAppBehaviorFixture, FrameUpdateDispatchAppliesTransformCommand)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;
    ASSERT_TRUE(app.load_scene(scene_load_desc(project.manifest)));
    ASSERT_EQ(app.active_behavior_binding_count(), 1u);

    const std::optional<wz::math::Vec3> before =
        app.node_local_translation("mover");
    ASSERT_TRUE(before.has_value()) << "mover node missing from scene";
    EXPECT_FLOAT_EQ(before->y, 0.0f);

    // One tick: behavior writes ADD_LOCAL_TRANSLATION (0,+1,0); apply moves the
    // node and the change is written back to the authored scene node.
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);

    const std::optional<wz::math::Vec3> after_one =
        app.node_local_translation("mover");
    ASSERT_TRUE(after_one.has_value());
    EXPECT_FLOAT_EQ(after_one->y, 1.0f)
        << "frame.update transform command did not apply to scene_nodes_";

    // It is per-frame: a second tick advances it again.
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    const std::optional<wz::math::Vec3> after_two =
        app.node_local_translation("mover");
    ASSERT_TRUE(after_two.has_value());
    EXPECT_FLOAT_EQ(after_two->y, 2.0f)
        << "behaviors did not dispatch on the second frame";
}

// ─── Live behavior-binding authoring (the host-ABI verbs' engine-thread apply) ─
// These exercise WozzitsApp_v1's behavior-authoring methods — the apply layer
// the deferred host-ABI verbs (wz_editor_runtime_*_node_behavior) call on the
// engine thread. Starting from the fixture's "blank" node (NO behavior), adding
// the "move_up_on_frame" module must materialize a live binding that moves the
// node; toggling enabled / editing config / removing must take effect on the
// next tick. This is the same direct-drive approach as the dispatch test above,
// proving the authoring path without standing up the cross-thread render loop.

TEST_F(WozzitsAppBehaviorFixture, AddBehaviorMaterializesAndRunsOnBlankNode)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;
    ASSERT_TRUE(app.load_scene(scene_load_desc(project.manifest)));

    // "blank" starts with no behavior; the scene's only binding is "mover".
    ASSERT_EQ(app.active_behavior_binding_count(), 1u);
    const std::optional<wz::math::Vec3> before =
        app.node_local_translation("blank");
    ASSERT_TRUE(before.has_value()) << "blank node missing from scene";
    EXPECT_FLOAT_EQ(before->y, 0.0f);

    // Add the move-up behavior to "blank": a binding is minted + materialized.
    const wz::engine::assets::SceneAddBehaviorResult added =
        app.add_node_behavior("blank", "move_up_on_frame");
    ASSERT_TRUE(added.ok) << added.error;
    EXPECT_FALSE(added.binding_id.empty())
        << "add_node_behavior must mint a stable binding id";
    EXPECT_EQ(app.active_behavior_binding_count(), 2u)
        << "the added binding was not materialized into the behavior runtime";

    // One tick: the freshly added binding dispatches frame.update (it has no
    // events, so it falls back to the module's default channel) and moves blank.
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    const std::optional<wz::math::Vec3> after_add =
        app.node_local_translation("blank");
    ASSERT_TRUE(after_add.has_value());
    EXPECT_FLOAT_EQ(after_add->y, 1.0f)
        << "the added behavior did not run on the blank node";

    // Disabling the binding stops it: the next tick does not advance it.
    ASSERT_TRUE(
        app.set_node_behavior_enabled("blank", added.binding_id, false));
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    const std::optional<wz::math::Vec3> after_disable =
        app.node_local_translation("blank");
    ASSERT_TRUE(after_disable.has_value());
    EXPECT_FLOAT_EQ(after_disable->y, 1.0f)
        << "a disabled behavior must not dispatch";

    // Re-enabling resumes it on the following tick.
    ASSERT_TRUE(
        app.set_node_behavior_enabled("blank", added.binding_id, true));
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    const std::optional<wz::math::Vec3> after_reenable =
        app.node_local_translation("blank");
    ASSERT_TRUE(after_reenable.has_value());
    EXPECT_FLOAT_EQ(after_reenable->y, 2.0f)
        << "a re-enabled behavior must dispatch again";

    // Removing the binding takes it out of the runtime: count drops back, and
    // the node stops moving.
    ASSERT_TRUE(app.remove_node_behavior("blank", added.binding_id));
    EXPECT_EQ(app.active_behavior_binding_count(), 1u)
        << "removed binding still present in the behavior runtime";
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    const std::optional<wz::math::Vec3> after_remove =
        app.node_local_translation("blank");
    ASSERT_TRUE(after_remove.has_value());
    EXPECT_FLOAT_EQ(after_remove->y, 2.0f)
        << "a removed behavior must no longer dispatch";
}

// set/clear config + set-fields/set-events apply (and survive a rebuild). The
// move-up module ignores config, so this asserts the authoring mutations land
// and the binding keeps running rather than a config-driven movement change.
TEST_F(WozzitsAppBehaviorFixture, EditBehaviorFieldsConfigAndEventsApply)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;
    ASSERT_TRUE(app.load_scene(scene_load_desc(project.manifest)));

    const wz::engine::assets::SceneAddBehaviorResult added =
        app.add_node_behavior("blank", "move_up_on_frame");
    ASSERT_TRUE(added.ok) << added.error;
    const std::string& id = added.binding_id;

    // set-fields: relabel + keep the module (so it keeps running).
    EXPECT_TRUE(
        app.set_node_behavior_fields(
            "blank", id, "Renamed", "move_up_on_frame"));

    // set-events: an explicit frame.update keeps it dispatching.
    EXPECT_TRUE(
        app.set_node_behavior_events("blank", id, { "frame.update" }));

    // set-config (string then overwrite as bool) + a second key, then clear one.
    wz::engine::assets::SceneBehaviorConfigValue speed;
    speed.key = "speed";
    speed.kind = wz::engine::assets::SceneBehaviorConfigValueKind::Number;
    speed.number_value = 2.0;
    EXPECT_TRUE(app.set_node_behavior_config("blank", id, speed));

    wz::engine::assets::SceneBehaviorConfigValue loop;
    loop.key = "loop";
    loop.kind = wz::engine::assets::SceneBehaviorConfigValueKind::Bool;
    loop.bool_value = true;
    EXPECT_TRUE(app.set_node_behavior_config("blank", id, loop));

    // Overwrite "speed" in place (same key) — still one "speed" entry.
    wz::engine::assets::SceneBehaviorConfigValue speed2;
    speed2.key = "speed";
    speed2.kind = wz::engine::assets::SceneBehaviorConfigValueKind::Number;
    speed2.number_value = 5.0;
    EXPECT_TRUE(app.set_node_behavior_config("blank", id, speed2));

    // clear an existing key succeeds; clearing a missing key fails.
    EXPECT_TRUE(app.clear_node_behavior_config("blank", id, "loop"));
    EXPECT_FALSE(app.clear_node_behavior_config("blank", id, "missing"));

    // Editing a non-existent binding id fails (no node/binding matched).
    EXPECT_FALSE(
        app.set_node_behavior_enabled("blank", "no.such.binding", false));

    // After all the edits the binding is still live and runs.
    EXPECT_EQ(app.active_behavior_binding_count(), 2u);
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    const std::optional<wz::math::Vec3> after =
        app.node_local_translation("blank");
    ASSERT_TRUE(after.has_value());
    EXPECT_FLOAT_EQ(after->y, 1.0f)
        << "the binding stopped running after field/config/event edits";
}

// ─── Live optional-component authoring (the host-ABI component verbs' apply) ──
// These exercise WozzitsApp_v1::add_node_component / remove_node_component — the
// engine-thread apply behind the deferred host-ABI verbs
// (wz_editor_runtime_add_node_component / _remove_node_component). Starting from
// the fixture's "blank" node, adding a component must make it present (observed
// via node_has_component, the same presence the editor's snapshot component list
// surfaces) and removing it must make it absent. An unknown kind is a no-op.

TEST_F(WozzitsAppBehaviorFixture, AddRemoveOptionalComponentsOnBlankNode)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;
    ASSERT_TRUE(app.load_scene(scene_load_desc(project.manifest)));

    // The blank node starts with none of the optional components.
    ASSERT_FALSE(app.node_has_component("blank", "camera"));
    ASSERT_FALSE(app.node_has_component("blank", "proximity"));
    ASSERT_FALSE(app.node_has_component("blank", "motion"));

    // camera: add → present, remove → gone.
    EXPECT_TRUE(app.add_node_component("blank", "camera"));
    EXPECT_TRUE(app.node_has_component("blank", "camera"));
    EXPECT_TRUE(app.remove_node_component("blank", "camera"));
    EXPECT_FALSE(app.node_has_component("blank", "camera"));

    // proximity: add → present, remove → gone.
    EXPECT_TRUE(app.add_node_component("blank", "proximity"));
    EXPECT_TRUE(app.node_has_component("blank", "proximity"));
    EXPECT_TRUE(app.remove_node_component("blank", "proximity"));
    EXPECT_FALSE(app.node_has_component("blank", "proximity"));

    // motion: add → present, remove → gone.
    EXPECT_TRUE(app.add_node_component("blank", "motion"));
    EXPECT_TRUE(app.node_has_component("blank", "motion"));
    EXPECT_TRUE(app.remove_node_component("blank", "motion"));
    EXPECT_FALSE(app.node_has_component("blank", "motion"));

    // Components are independent: adding two leaves both present, and removing
    // one leaves the other.
    EXPECT_TRUE(app.add_node_component("blank", "collision"));
    EXPECT_TRUE(app.add_node_component("blank", "renderable"));
    EXPECT_TRUE(app.node_has_component("blank", "collision"));
    EXPECT_TRUE(app.node_has_component("blank", "renderable"));
    EXPECT_TRUE(app.remove_node_component("blank", "collision"));
    EXPECT_FALSE(app.node_has_component("blank", "collision"));
    EXPECT_TRUE(app.node_has_component("blank", "renderable"))
        << "removing one component must not disturb another";
    EXPECT_TRUE(app.remove_node_component("blank", "renderable"));

    // Adding a behavior (a separate slot) does not register as a component, and
    // these component edits never created a behavior binding either: the only
    // live binding remains the scene's "mover".
    EXPECT_EQ(app.active_behavior_binding_count(), 1u)
        << "optional-component edits must not touch the behavior runtime";

    // Fail closed: unknown kind and missing node are no-ops returning false.
    EXPECT_FALSE(app.add_node_component("blank", "not_a_component"));
    EXPECT_FALSE(app.remove_node_component("blank", "not_a_component"));
    EXPECT_FALSE(app.add_node_component("no_such_node", "camera"));
    EXPECT_FALSE(app.node_has_component("blank", "not_a_component"));
}
