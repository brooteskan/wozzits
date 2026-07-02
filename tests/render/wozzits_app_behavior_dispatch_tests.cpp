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
    // sim node, and node_local_translation reports it (derived from the polytree,
    // #221 — no per-frame write-back into scene_nodes_).
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);

    const std::optional<wz::math::Vec3> after_one =
        app.node_local_translation("mover");
    ASSERT_TRUE(after_one.has_value());
    EXPECT_FLOAT_EQ(after_one->y, 1.0f)
        << "frame.update transform command did not move the mover node";

    // It is per-frame: a second tick advances it again.
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    const std::optional<wz::math::Vec3> after_two =
        app.node_local_translation("mover");
    ASSERT_TRUE(after_two.has_value());
    EXPECT_FLOAT_EQ(after_two->y, 2.0f)
        << "behaviors did not dispatch on the second frame";
}

// #221 core invariant: a sim-driven transform change is visible in the render
// world transforms WITHOUT scene_nodes_ ever being mutated. The renderer now
// reads the live polytree (scene_world_transforms), and the per-frame Mat4->TRS
// ->Mat4 write-back into scene_nodes_ is gone — so the STORED authored transform
// stays put while the drawn/world transform moves.
TEST_F(WozzitsAppBehaviorFixture, SimMovementVisibleInRenderWithoutMutatingSceneNodes)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;
    ASSERT_TRUE(app.load_scene(scene_load_desc(project.manifest)));
    ASSERT_EQ(app.active_behavior_binding_count(), 1u);

    // The stored authored transform + the render world transform agree at start.
    const std::optional<wz::math::Vec3> stored_start =
        app.stored_node_local_translation("mover");
    ASSERT_TRUE(stored_start.has_value()) << "mover node missing from scene";
    EXPECT_FLOAT_EQ(stored_start->y, 0.0f);

    const std::optional<wz::math::Mat4> world_start =
        app.node_world_transform("mover");
    ASSERT_TRUE(world_start.has_value());
    EXPECT_FLOAT_EQ(world_start->m[13], 0.0f);  // world-space Y translation

    // Two ticks: the mover behavior raises the node by +1 y each frame.
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);

    // The render world transform moved (the renderer draws the sim-current pose).
    const std::optional<wz::math::Mat4> world_after =
        app.node_world_transform("mover");
    ASSERT_TRUE(world_after.has_value());
    EXPECT_FLOAT_EQ(world_after->m[13], 2.0f)
        << "render world transform did not follow the sim movement";

    // ...but scene_nodes_ (the STORED authored data) was never mutated.
    const std::optional<wz::math::Vec3> stored_after =
        app.stored_node_local_translation("mover");
    ASSERT_TRUE(stored_after.has_value());
    EXPECT_FLOAT_EQ(stored_after->y, 0.0f)
        << "scene_nodes_ was mutated by the sim — the write-back should be gone";
}

// #221: set_node_motion_terrain_fields on a node that ALREADY has a live Motion
// record must patch the record IN PLACE — NOT rebuild the whole behavior runtime
// (a rebuild would reset behavior/sim state for a mere field tweak). The proof:
// the "mover" behavior advances +1 y per frame and accrues that pose in the live
// polytree; after adding a Motion component (the rebuild-once ADD path) and
// ticking to accumulate a pose, a SECOND set_node_motion_terrain_fields (the
// in-place path) must (a) take effect — the field is observably updated — and
// (b) leave the accrued pose + dispatch cadence intact: the next tick advances
// the mover from where it was, not from a reset. A full rebuild is not needed
// for a field change, so the mover keeps counting up unbroken.
TEST_F(WozzitsAppBehaviorFixture, MotionTerrainFieldsInPlaceKeepsSimState)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;
    ASSERT_TRUE(app.load_scene(scene_load_desc(project.manifest)));
    ASSERT_EQ(app.active_behavior_binding_count(), 1u);

    // First call ADDS the Motion component to "mover" (no live record yet), so it
    // takes the rebuild-once path. The behavior survives the rebuild.
    ASSERT_TRUE(app.set_node_motion_terrain_fields(
        "mover",
        /*terrain_constrained*/ false,
        /*ride_height*/ 1.0f,
        /*footprint_radius*/ 2.0f,
        /*align_to_surface*/ false,
        /*alignment_strength*/ 0.5f));
    ASSERT_EQ(app.active_behavior_binding_count(), 1u)
        << "adding a Motion component must not drop the behavior binding";

    // Tick twice: the mover accrues +2 y in the live polytree.
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    const std::optional<wz::math::Vec3> mid =
        app.node_local_translation("mover");
    ASSERT_TRUE(mid.has_value());
    EXPECT_FLOAT_EQ(mid->y, 2.0f)
        << "the mover did not accumulate its per-frame movement";

    // Second call — now the Motion record EXISTS both authored and live, so this
    // is the in-place patch path. It must NOT rebuild (which would re-run init and
    // could disturb the accrued pose the preservation map would otherwise have to
    // restore). The edit takes effect: the authored field is updated.
    ASSERT_TRUE(app.set_node_motion_terrain_fields(
        "mover",
        /*terrain_constrained*/ true,
        /*ride_height*/ 3.5f,
        /*footprint_radius*/ 4.0f,
        /*align_to_surface*/ true,
        /*alignment_strength*/ 0.25f));
    const wz::engine::assets::SceneMotionAsset* motion =
        app.node_motion("mover");
    ASSERT_NE(motion, nullptr);
    EXPECT_TRUE(motion->terrain_constrained);
    EXPECT_FLOAT_EQ(motion->terrain_ride_height, 3.5f)
        << "the in-place motion field edit did not take effect";

    // The accrued pose is intact right after the field edit (no reset to 0).
    const std::optional<wz::math::Vec3> after_edit =
        app.node_local_translation("mover");
    ASSERT_TRUE(after_edit.has_value());
    EXPECT_FLOAT_EQ(after_edit->y, 2.0f)
        << "the field edit reset the accrued sim pose — a rebuild leaked through";

    // And dispatch continues seamlessly from where it was: the next tick advances
    // the mover to 3, not back to 1 (which a re-init would produce). The behavior
    // was not reset — proving no full rebuild happened for the field tweak.
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    const std::optional<wz::math::Vec3> after_tick =
        app.node_local_translation("mover");
    ASSERT_TRUE(after_tick.has_value());
    EXPECT_FLOAT_EQ(after_tick->y, 3.0f)
        << "dispatch did not continue from the accrued pose after the in-place "
           "motion field edit";
}

// An input-driven behavior ("input.*") must fire only when WozzitsApp_v1 builds
// and routes input events in its tick. A controller-axis InputState moves the
// bound node; an empty InputState does not. This proves the new runtime's
// build_input_event_frame wiring (without it the tank controller never moves).
TEST_F(WozzitsAppBehaviorFixture, InputEventDispatchDrivesInputBehavior)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;
    ASSERT_TRUE(app.load_scene(scene_load_desc(project.manifest)));

    // Bind the input-subscribed behavior to "blank" (no other behavior on it).
    const wz::engine::assets::SceneAddBehaviorResult added =
        app.add_node_behavior("blank", "move_on_input");
    ASSERT_TRUE(added.ok) << added.error;

    const std::optional<wz::math::Vec3> before =
        app.node_local_translation("blank");
    ASSERT_TRUE(before.has_value()) << "blank node missing from scene";
    const float x0 = before->x;

    // An empty input frame produces no input events, so the behavior never fires.
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    const std::optional<wz::math::Vec3> after_empty =
        app.node_local_translation("blank");
    ASSERT_TRUE(after_empty.has_value());
    EXPECT_FLOAT_EQ(after_empty->x, x0)
        << "input behavior moved with no input events (it shouldn't)";

    // A connected controller with a changed axis generates a
    // CONTROLLER_AXIS_CHANGED event; the runtime must build + route it so the
    // behavior fires and moves the node +1 in X.
    wz::input::InputState input{};
    input.controllers.count = 1u;
    input.controllers.controllers[0].connected = true;
    input.controllers.controllers[0].axes_changed[1] = true;
    input.controllers.controllers[0].axes[1] = 1.0f;
    app.simulation_tick(input, 1.0f / 60.0f);

    const std::optional<wz::math::Vec3> after_input =
        app.node_local_translation("blank");
    ASSERT_TRUE(after_input.has_value());
    EXPECT_FLOAT_EQ(after_input->x, x0 + 1.0f)
        << "input event was not built/routed to the behavior in the tick";
}

// reload_behavior_modules must rebuild the registry (built-ins + project DLLs)
// and re-materialize the scene's behavior binding in place — without a restart —
// and the reloaded behavior must still dispatch. This is the engine apply the
// editor's "Rebuild Behavior Modules" verb (wz_host_runtime_reload_behavior_
// modules) drives after it recompiles the project's behavior sources.
TEST_F(WozzitsAppBehaviorFixture, ReloadBehaviorModulesRebuildsAndStillDispatches)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;
    ASSERT_TRUE(app.load_scene(scene_load_desc(project.manifest)));
    ASSERT_EQ(app.active_behavior_binding_count(), 1u);

    // The registered modules (built-ins + the project DLL) are listable for the
    // editor's "add behavior" picker.
    const std::vector<std::string> modules_before = app.behavior_module_names();
    EXPECT_FALSE(modules_before.empty())
        << "registered behavior modules should be listable";

    // Reload from the same staged module folder: the registry is cleared and
    // rebuilt (built-ins re-registered, the project DLL reloaded) and the
    // behavior scene re-materialized. The one binding must survive — not vanish
    // (lost built-ins / unbuilt scene) and not duplicate (stale registrations).
    app.reload_behavior_modules(project.manifest.behavior_module_folder);
    EXPECT_EQ(app.active_behavior_binding_count(), 1u)
        << "reload must re-materialize exactly the scene's one binding";
    EXPECT_EQ(app.behavior_module_names(), modules_before)
        << "reload should restore the same registered modules";

    // And the reloaded behavior still dispatches: one tick moves the node up.
    const std::optional<wz::math::Vec3> before =
        app.node_local_translation("mover");
    ASSERT_TRUE(before.has_value()) << "mover node missing from scene";
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    const std::optional<wz::math::Vec3> after =
        app.node_local_translation("mover");
    ASSERT_TRUE(after.has_value());
    EXPECT_FLOAT_EQ(after->y, before->y + 1.0f)
        << "reloaded behavior did not dispatch after reload_behavior_modules";
}

// ─── Live behavior-binding authoring (the host-ABI verbs' engine-thread apply) ─
// These exercise WozzitsApp_v1's behavior-authoring methods — the apply layer
// the deferred host-ABI verbs (wz_host_runtime_*_node_behavior) call on the
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
// (wz_host_runtime_add_node_component / _remove_node_component). Starting from
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
    EXPECT_TRUE(app.add_node_component("blank", "motion"));
    EXPECT_TRUE(app.node_has_component("blank", "collision"));
    EXPECT_TRUE(app.node_has_component("blank", "motion"));
    EXPECT_TRUE(app.remove_node_component("blank", "collision"));
    EXPECT_FALSE(app.node_has_component("blank", "collision"));
    EXPECT_TRUE(app.node_has_component("blank", "motion"))
        << "removing one component must not disturb another";
    EXPECT_TRUE(app.remove_node_component("blank", "motion"));

    // Adding a behavior (a separate slot) does not register as a component, and
    // these component edits never created a behavior binding either: the only
    // live binding remains the scene's "mover".
    EXPECT_EQ(app.active_behavior_binding_count(), 1u)
        << "optional-component edits must not touch the behavior runtime";

    // The generic component verbs no longer accept "renderable": the legacy
    // embedded slot is not editor-authorable, and the preferred asset-graph
    // renderable has its own verb (see SetNodeRenderableAssetOnBlankNode).
    EXPECT_FALSE(app.add_node_component("blank", "renderable"))
        << "renderable must not be reachable via the generic component verbs";
    EXPECT_FALSE(app.remove_node_component("blank", "renderable"));
    EXPECT_FALSE(app.node_has_component("blank", "renderable"));

    // Fail closed: unknown kind and missing node are no-ops returning false.
    EXPECT_FALSE(app.add_node_component("blank", "not_a_component"));
    EXPECT_FALSE(app.remove_node_component("blank", "not_a_component"));
    EXPECT_FALSE(app.add_node_component("no_such_node", "camera"));
    EXPECT_FALSE(app.node_has_component("blank", "not_a_component"));
}

// ─── Preferred asset-graph renderable authoring (the host-ABI renderable verb) ─
// Exercises WozzitsApp_v1::set_node_renderable_asset — the engine-thread apply
// behind wz_host_runtime_set_node_renderable_asset. Binding a non-zero
// asset-graph node id sets the node's renderable_asset_node_id; binding 0 clears
// it. This is the ONLY editor path to the renderable; the legacy embedded slot
// is untouched and unreachable from the generic component verbs.

TEST_F(WozzitsAppBehaviorFixture, SetNodeRenderableAssetOnBlankNode)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;
    ASSERT_TRUE(app.load_scene(scene_load_desc(project.manifest)));

    // The blank node starts with no preferred renderable bound.
    ASSERT_FALSE(app.node_renderable_asset_node_id("blank").has_value());

    // Bind an authored asset-graph node id → renderable_asset_node_id present.
    EXPECT_TRUE(app.set_node_renderable_asset("blank", 42));
    const auto bound = app.node_renderable_asset_node_id("blank");
    ASSERT_TRUE(bound.has_value());
    EXPECT_EQ(*bound, 42u);

    // Rebinding to a different id replaces it.
    EXPECT_TRUE(app.set_node_renderable_asset("blank", 7));
    const auto rebound = app.node_renderable_asset_node_id("blank");
    ASSERT_TRUE(rebound.has_value());
    EXPECT_EQ(*rebound, 7u);

    // Binding 0 clears the renderable (back to nullopt).
    EXPECT_TRUE(app.set_node_renderable_asset("blank", 0));
    EXPECT_FALSE(app.node_renderable_asset_node_id("blank").has_value());

    // Authoring the renderable never touched the behavior runtime.
    EXPECT_EQ(app.active_behavior_binding_count(), 1u)
        << "renderable authoring must not touch the behavior runtime";

    // Fail closed: a missing node is a no-op returning false, with no binding.
    EXPECT_FALSE(app.set_node_renderable_asset("no_such_node", 42));
    EXPECT_FALSE(app.node_renderable_asset_node_id("no_such_node").has_value());
}

// ─── Behavior-driven deferred add_child (#204 probe) ──────────────────────────
// A behavior issues the SAME runtime authoring op the host's add_child uses —
// spawning a child node — as a deferred, fire-and-forget request that the
// runtime applies at the frame boundary through WozzitsApp_v1::add_child_node
// (the one shared apply path). The "spawn_child_on_frame" module (staged in the
// fixture DLL) calls wz_spawn_child on its own entity on frame.update; binding it
// to "blank" and ticking once must add exactly one child UNDER "blank", visible
// on the authored scene (what the renderer draws from). This proves the deferred
// authoring seam end-to-end without standing up the cross-thread render loop —
// the same direct-drive approach as the dispatch test above.
TEST_F(WozzitsAppBehaviorFixture, BehaviorSpawnChildAddsChildAtFrameBoundary)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;
    ASSERT_TRUE(app.load_scene(scene_load_desc(project.manifest)));

    // "blank" starts childless; the scene's only binding is "mover".
    ASSERT_EQ(app.active_behavior_binding_count(), 1u);
    ASSERT_EQ(app.child_node_count("blank"), 0u)
        << "blank node must start with no children";

    // Bind the spawn-child behavior to "blank": a binding is materialized.
    const wz::engine::assets::SceneAddBehaviorResult added =
        app.add_node_behavior("blank", "spawn_child_on_frame");
    ASSERT_TRUE(added.ok) << added.error;
    EXPECT_EQ(app.active_behavior_binding_count(), 2u)
        << "the spawn-child binding was not materialized";

    // One tick: the behavior queues a deferred spawn_child during dispatch; the
    // runtime drains it at the frame boundary via add_child_node (the same apply
    // the host's add_child uses). Exactly one new child node now sits under
    // "blank" — one dispatch issues one deferred spawn.
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    EXPECT_EQ(app.child_node_count("blank"), 1u)
        << "the behavior's deferred spawn_child did not add a child node";

    // It is genuinely per-frame and the add_child's behavior-runtime rebuild did
    // not break dispatch: a second tick adds a second child. This also confirms
    // the spawn was deferred (it did not mutate the scene mid-dispatch, which
    // would have been a reentrancy hazard while the engine iterated behaviors).
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    EXPECT_EQ(app.child_node_count("blank"), 2u)
        << "the spawn-child behavior stopped dispatching after the structural "
           "rebuild triggered by the first add_child";
}

// ─── Behavior-driven deferred remove_node (#204) ──────────────────────────────
// A behavior issues the SAME runtime authoring op the host's remove uses —
// removing its own node — as a deferred, fire-and-forget request the runtime
// applies at the frame boundary through WozzitsApp_v1::remove_node (the one
// shared apply path). A child node is added under "blank" and bound to
// "remove_node_on_frame", which calls wz_self_remove_node on frame.update;
// ticking once must drop child_node_count("blank") back to 0, visible on the
// authored scene. Same direct-drive approach as the spawn test above.
TEST_F(WozzitsAppBehaviorFixture, BehaviorRemoveNodeRemovesSelfAtFrameBoundary)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;
    ASSERT_TRUE(app.load_scene(scene_load_desc(project.manifest)));

    // "blank" starts childless; the scene's only binding is "mover".
    ASSERT_EQ(app.active_behavior_binding_count(), 1u);
    ASSERT_EQ(app.child_node_count("blank"), 0u)
        << "blank node must start with no children";

    // Add a child under "blank" and bind the self-remove behavior to THAT child.
    const wz::engine::assets::SceneAddChildResult child =
        app.add_child_node("blank");
    ASSERT_TRUE(child.ok) << child.error;
    ASSERT_EQ(app.child_node_count("blank"), 1u);
    const wz::engine::assets::SceneAddBehaviorResult added =
        app.add_node_behavior(child.new_id, "remove_node_on_frame");
    ASSERT_TRUE(added.ok) << added.error;
    EXPECT_EQ(app.active_behavior_binding_count(), 2u)
        << "the remove-node binding was not materialized";

    // One tick: the behavior queues a deferred remove_node for its own node
    // during dispatch; the runtime drains it at the frame boundary via
    // remove_node (the same apply the host's remove uses). The child is gone.
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    EXPECT_EQ(app.child_node_count("blank"), 0u)
        << "the behavior's deferred remove_node did not remove the child node";

    // The removed node's binding left the runtime with it (back to just
    // "mover"), confirming the structural remove took effect.
    EXPECT_EQ(app.active_behavior_binding_count(), 1u)
        << "the removed node's behavior binding is still live";
}

// ─── Behavior-driven deferred set_renderable_asset (#204) ─────────────────────
// A behavior issues the SAME runtime authoring op the host uses — setting its
// node's preferred asset-graph renderable — as a deferred, fire-and-forget
// request the runtime applies at the frame boundary through
// WozzitsApp_v1::set_node_renderable_asset (the one shared apply path).
// "set_renderable_on_frame" calls wz_self_set_renderable_asset(42) on
// frame.update; binding it to "blank" and ticking once must make
// node_renderable_asset_node_id("blank") == 42. set_node_renderable_asset only
// sets the field (no asset-DAG recompile), so an arbitrary id is fine.
TEST_F(WozzitsAppBehaviorFixture, BehaviorSetRenderableAssetAtFrameBoundary)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;
    ASSERT_TRUE(app.load_scene(scene_load_desc(project.manifest)));

    // "blank" starts with no preferred renderable bound.
    ASSERT_EQ(app.active_behavior_binding_count(), 1u);
    ASSERT_FALSE(app.node_renderable_asset_node_id("blank").has_value())
        << "blank node must start with no preferred renderable";

    // Bind the set-renderable behavior to "blank": a binding is materialized.
    const wz::engine::assets::SceneAddBehaviorResult added =
        app.add_node_behavior("blank", "set_renderable_on_frame");
    ASSERT_TRUE(added.ok) << added.error;
    EXPECT_EQ(app.active_behavior_binding_count(), 2u)
        << "the set-renderable binding was not materialized";

    // One tick: the behavior queues a deferred set_renderable_asset(42) during
    // dispatch; the runtime drains it at the frame boundary via
    // set_node_renderable_asset (the same apply the host uses). The node's
    // preferred asset-graph renderable is now 42.
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    const auto bound = app.node_renderable_asset_node_id("blank");
    ASSERT_TRUE(bound.has_value())
        << "the behavior's deferred set_renderable_asset did not bind a "
           "renderable";
    EXPECT_EQ(*bound, 42u);
}

// ─── Behavior-driven deferred reparent (#204) ─────────────────────────────────
// A behavior issues the SAME runtime authoring op the host's reparent uses —
// reparenting its own node — as a deferred, fire-and-forget request the runtime
// applies at the frame boundary through WozzitsApp_v1::reparent_node (the one
// shared apply path). A child node is added under "blank" and bound to
// "reparent_self_to_top_on_frame", which calls wz_self_detach_to_top_level
// (reparent to WZ_INVALID_BEHAVIOR_ENTITY = top level) on frame.update; ticking
// once must drop child_node_count("blank") to 0 (the child DETACHED but still
// exists, unlike the remove test). Same direct-drive approach as the tests
// above.
TEST_F(WozzitsAppBehaviorFixture, BehaviorReparentDetachesSelfAtFrameBoundary)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;
    ASSERT_TRUE(app.load_scene(scene_load_desc(project.manifest)));

    // "blank" starts childless; the scene's only binding is "mover".
    ASSERT_EQ(app.active_behavior_binding_count(), 1u);
    ASSERT_EQ(app.child_node_count("blank"), 0u)
        << "blank node must start with no children";

    // Add a child under "blank" and bind the self-reparent behavior to THAT
    // child.
    const wz::engine::assets::SceneAddChildResult child =
        app.add_child_node("blank");
    ASSERT_TRUE(child.ok) << child.error;
    ASSERT_EQ(app.child_node_count("blank"), 1u);
    const wz::engine::assets::SceneAddBehaviorResult added =
        app.add_node_behavior(child.new_id, "reparent_self_to_top_on_frame");
    ASSERT_TRUE(added.ok) << added.error;
    EXPECT_EQ(app.active_behavior_binding_count(), 2u)
        << "the reparent binding was not materialized";

    // One tick: the behavior queues a deferred reparent-to-top for its own node
    // during dispatch; the runtime drains it at the frame boundary via
    // reparent_node (the same apply the host's reparent uses). The child is no
    // longer under "blank".
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    EXPECT_EQ(app.child_node_count("blank"), 0u)
        << "the behavior's deferred reparent did not detach the child node";

    // Unlike remove, the node still EXISTS (it moved to the top level), so its
    // binding is still live — the runtime still carries both bindings.
    EXPECT_EQ(app.active_behavior_binding_count(), 2u)
        << "the reparented node's behavior binding must remain live (it was "
           "detached, not removed)";
}

// ─── Behavior-driven deferred add_component (#204) ────────────────────────────
// A behavior issues the SAME runtime authoring op the host uses — adding an
// optional component to its own node — as a deferred, fire-and-forget request
// the runtime applies at the frame boundary through
// WozzitsApp_v1::add_node_component (the one shared apply path).
// "add_proximity_on_frame" calls wz_self_add_node_component(.., "proximity") on
// frame.update; binding it to "blank" and ticking once must make
// node_has_component("blank","proximity") true.
TEST_F(WozzitsAppBehaviorFixture, BehaviorAddComponentAddsAtFrameBoundary)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;
    ASSERT_TRUE(app.load_scene(scene_load_desc(project.manifest)));

    // "blank" starts without the proximity component.
    ASSERT_EQ(app.active_behavior_binding_count(), 1u);
    ASSERT_FALSE(app.node_has_component("blank", "proximity"))
        << "blank node must start with no proximity component";

    // Bind the add-component behavior to "blank": a binding is materialized.
    const wz::engine::assets::SceneAddBehaviorResult added =
        app.add_node_behavior("blank", "add_proximity_on_frame");
    ASSERT_TRUE(added.ok) << added.error;
    EXPECT_EQ(app.active_behavior_binding_count(), 2u)
        << "the add-component binding was not materialized";

    // One tick: the behavior queues a deferred add_node_component("proximity")
    // during dispatch; the runtime drains it at the frame boundary via
    // add_node_component (the same apply the host uses). The component is now
    // present.
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    EXPECT_TRUE(app.node_has_component("blank", "proximity"))
        << "the behavior's deferred add_node_component did not add the "
           "component";
}

// ─── Behavior-driven deferred remove_component (#204) ─────────────────────────
// A behavior issues the SAME runtime authoring op the host uses — removing an
// optional component from its own node — as a deferred, fire-and-forget request
// the runtime applies at the frame boundary through
// WozzitsApp_v1::remove_node_component (the one shared apply path). The test
// pre-adds "proximity" to "blank", binds "remove_proximity_on_frame" (which
// calls wz_self_remove_node_component(.., "proximity") on frame.update), and
// asserts node_has_component("blank","proximity") becomes false after one tick.
TEST_F(WozzitsAppBehaviorFixture, BehaviorRemoveComponentRemovesAtFrameBoundary)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;
    ASSERT_TRUE(app.load_scene(scene_load_desc(project.manifest)));

    // Pre-add the proximity component to "blank" (the host-side apply, directly).
    ASSERT_EQ(app.active_behavior_binding_count(), 1u);
    ASSERT_TRUE(app.add_node_component("blank", "proximity"));
    ASSERT_TRUE(app.node_has_component("blank", "proximity"))
        << "pre-condition: blank must carry the proximity component";

    // Bind the remove-component behavior to "blank": a binding is materialized.
    const wz::engine::assets::SceneAddBehaviorResult added =
        app.add_node_behavior("blank", "remove_proximity_on_frame");
    ASSERT_TRUE(added.ok) << added.error;
    EXPECT_EQ(app.active_behavior_binding_count(), 2u)
        << "the remove-component binding was not materialized";

    // One tick: the behavior queues a deferred
    // remove_node_component("proximity") during dispatch; the runtime drains it
    // at the frame boundary via remove_node_component (the same apply the host
    // uses). The component is gone.
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    EXPECT_FALSE(app.node_has_component("blank", "proximity"))
        << "the behavior's deferred remove_node_component did not remove the "
           "component";
}

// ─── Scene-source verbs (issue #213): the WozzitsApp_v1 apply layer ──────────
// These exercise WozzitsApp_v1's scene-source authoring methods on-device — the
// apply behind the host-ABI verb wz_host_runtime_set_node_scene_source +
// flatten. The algorithmic graft/expand/flatten logic is covered device-free in
// asset_scene/scene_source_expansion_tests; here we assert the app-level verbs'
// observable state + guard (fail-closed) behavior on a real app, and that
// authoring a scene source does not disturb the running behavior runtime.

TEST_F(WozzitsAppBehaviorFixture, SceneSourceVerbsGuardAndStateOnApp)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;
    ASSERT_TRUE(app.load_scene(scene_load_desc(project.manifest)));
    ASSERT_EQ(app.active_behavior_binding_count(), 1u);

    // Guard: setting/flattening a scene source on a missing node fails closed.
    EXPECT_FALSE(app.set_node_scene_source("does_not_exist", 7u));
    EXPECT_FALSE(app.flatten_scene_source("does_not_exist"));

    // A real node with no authored scene source: the accessor reports none, and
    // flatten is a fail-closed no-op (nothing to expand).
    EXPECT_FALSE(app.node_scene_source_node_id("blank").has_value());
    EXPECT_FALSE(app.flatten_scene_source("blank"))
        << "flatten with no scene source must be a no-op (fail closed)";

    // Set an authored scene-source node id on "blank". The graph has no node
    // with that id, so it bridges to no resolved key + grafts nothing — but the
    // authored intent is recorded (and observable), mirroring the renderable
    // verb's set-then-bridge contract.
    EXPECT_TRUE(app.set_node_scene_source("blank", 1234u));
    ASSERT_TRUE(app.node_scene_source_node_id("blank").has_value());
    EXPECT_EQ(*app.node_scene_source_node_id("blank"), 1234u);
    // No children grafted (the authored node id resolves to no Scene asset).
    EXPECT_EQ(app.child_node_count("blank"), 0u);

    // Clearing (id 0) removes the authored reference; idempotent.
    EXPECT_TRUE(app.set_node_scene_source("blank", 0u));
    EXPECT_FALSE(app.node_scene_source_node_id("blank").has_value());

    // The behavior runtime is unaffected by the scene-source authoring + the
    // rebuilds it triggers: the scene's one binding still dispatches.
    const std::optional<wz::math::Vec3> before =
        app.node_local_translation("mover");
    ASSERT_TRUE(before.has_value());
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    const std::optional<wz::math::Vec3> after =
        app.node_local_translation("mover");
    ASSERT_TRUE(after.has_value());
    EXPECT_FLOAT_EQ(after->y, before->y + 1.0f)
        << "behavior dispatch must survive scene-source authoring";
}

// The GLB scene-source DESCRIPTOR verb (issue #213 Phase 3a): the apply behind
// the host-ABI verb wz_host_runtime_set_node_glb_scene_source. This mirrors
// SceneSourceVerbsGuardAndStateOnApp for the asset-graph-independent route —
// asserting the app-level verb's guard (fail-closed on a missing node) and that
// authoring a GLB descriptor on a real node does not disturb the running behavior
// runtime. The full graft/clear lifecycle is covered on the GLB fixture in
// wozzits_app_glb_scene_source_tests (SetGlbSceneSourceAuthorsAndClearsLive).
TEST_F(WozzitsAppBehaviorFixture, GlbSceneSourceVerbGuardAndStateOnApp)
{
    wz::app::WozzitsApp_v1 app(ctx);

    const auto project = load_test_project();
    ASSERT_TRUE(project.ok) << project.error;
    ASSERT_TRUE(app.load_scene(scene_load_desc(project.manifest)));
    ASSERT_EQ(app.active_behavior_binding_count(), 1u);

    // Guard: authoring a GLB descriptor on a missing node fails closed.
    EXPECT_FALSE(app.set_node_glb_scene_source(
        "does_not_exist",
        wz::engine::assets::SceneGLBSceneSource{ .path = "gltf/tank1.glb" }));

    // A real node with no scene source reports none; authoring the descriptor
    // resolves + grafts the GLB hierarchy under it (single default style — 3a).
    EXPECT_FALSE(app.node_has_glb_scene_source("blank"));
    EXPECT_TRUE(app.set_node_glb_scene_source(
        "blank",
        wz::engine::assets::SceneGLBSceneSource{
            .path = "gltf/tank1.glb",
            .scene_index = 0,
            .consume_mode =
                wz::engine::assets::SceneSourceConsumeMode::Instance,
        }));
    EXPECT_TRUE(app.node_has_glb_scene_source("blank"));
    EXPECT_TRUE(app.node_local_translation("blank/body").has_value())
        << "the GLB hierarchy was not grafted under the authored host";

    // Clearing (empty path) drops the descriptor + the grafted children.
    EXPECT_TRUE(app.set_node_glb_scene_source(
        "blank", wz::engine::assets::SceneGLBSceneSource{ .path = "" }));
    EXPECT_FALSE(app.node_has_glb_scene_source("blank"));
    EXPECT_FALSE(app.node_local_translation("blank/body").has_value());

    // The scene's own behavior binding (on "mover") survives the GLB authoring +
    // the graft/rebuild it triggers: dispatch still moves the node.
    const std::optional<wz::math::Vec3> before =
        app.node_local_translation("mover");
    ASSERT_TRUE(before.has_value());
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    const std::optional<wz::math::Vec3> after =
        app.node_local_translation("mover");
    ASSERT_TRUE(after.has_value());
    EXPECT_FLOAT_EQ(after->y, before->y + 1.0f)
        << "behavior dispatch must survive GLB scene-source authoring";
}
