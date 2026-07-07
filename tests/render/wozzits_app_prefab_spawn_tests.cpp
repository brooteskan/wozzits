// tests/render/wozzits_app_prefab_spawn_tests.cpp
//
// Coverage for runtime prefab spawning (the second prefab-system milestone) in
// the WozzitsApp_v1 shared runtime:
//   - a state-preserving behavior-scene rebuild: a structural edit (or a spawn)
//     rebuilds the runtime, and a pre-existing binding's instance state SURVIVES
//     (so its behavior is not re-initialized), while a freshly added binding
//     initializes fresh;
//   - a SPAWN_PREFAB behavior command grafts a registered prefab's subtree into
//     the running scene, its behavior initializes + runs the next tick, and the
//     spawner's own state is untouched;
//   - a spawn while a scene camera is active does NOT flip the active camera off
//     its anchor (#219 guard).
//
// The fixture (prefab_spawn_fixture) carries an "accumulator" node bound to the
// instance-state probe module accumulate_on_frame (Y == frames dispatched IFF
// its state survived every rebuild) and a "spawner" node bound to
// spawn_prefab_on_frame (emits SPAWN_PREFAB for "spawnling"). The prefab itself
// is registered programmatically by the test. It reuses the behavior fixture's
// staged module DLL (behavior_module_test_plugin).
//
// On-device test (WozzitsApp_v1 owns a RhiSceneRenderer needing a DX12 device).
// Skipped if no device can be created.

#include <gtest/gtest.h>

#include <engine/app/wozzits_app_v1.h>
#include <engine/app_context.h>
#include <engine/assets/scene/scene_asset_data.h>
#include <engine/behavior/drive_forward_behaviors.h>
#include <engine/project/project_manifest.h>

#include <asset/draft.h>
#include <gpu/gpu.h>
#include <input/input.h>

#include <vector>

namespace
{
    constexpr const char* kProjectRoot = "projects/prefab_spawn_fixture";

    struct WozzitsAppPrefabFixture : public ::testing::Test
    {
        wz::engine::AppContext ctx;
        bool initialized = false;

        void SetUp() override
        {
            wz::engine::AppDesc desc;
            desc.window = {
                "wozzits_app_v1_prefab_test", 256, 256, false, false };
            desc.resource_root = "resources";
            initialized = wz::engine::init(ctx, desc);
            if (!initialized) {
                GTEST_SKIP()
                    << "no GPU device — skipping on-device prefab spawn test";
            }
        }

        void TearDown() override
        {
            if (initialized) {
                wz::engine::shutdown(ctx);
            }
        }

        wz::app::WozzitsAppSceneLoadDesc scene_load_desc() const
        {
            const auto project = wz::engine::project::load_project_manifest(
                wz::engine::project::ProjectManifestLoadDesc{
                    .project_root = kProjectRoot,
                    .resource_root = ctx.assets->resource_root(),
                });
            EXPECT_TRUE(project.ok) << project.error;
            return wz::app::WozzitsAppSceneLoadDesc{
                .asset_graph = project.manifest.asset_graph_path,
                .scene = project.manifest.scene_path,
                .behavior_module_folder =
                    project.manifest.behavior_module_folder,
            };
        }

        // A 1-node prefab whose root binds accumulate_on_frame, so after a spawn
        // the spawned root's behavior initializes (counter 0) and its first tick
        // moves its Y to 1 — observable proof the spawned subtree's behavior runs.
        static std::vector<wz::engine::assets::SceneNodeAsset> make_spawnling()
        {
            wz::engine::assets::SceneNodeAsset root{};
            root.id = "spawnling_root";

            wz::engine::assets::SceneBehaviorAsset behavior{};
            behavior.module = "accumulate_on_frame";
            behavior.enabled = true;
            behavior.events = { "frame.update" };
            root.behavior = behavior;

            return { root };
        }

        // A 1-node prefab whose root binds the built-in drive_forward behavior +
        // a motion component, so after a spawn the grafted root drives itself
        // along its local +Z each tick — observable proof a spawned prefab not
        // only initializes but visibly MOVES. turn_rate 0 keeps it straight so the
        // path is predictable; plain (non-terrain) world motion so the integrated
        // forward step is directly visible in the spawn fixture (no terrain here).
        static std::vector<wz::engine::assets::SceneNodeAsset> make_driver()
        {
            wz::engine::assets::SceneNodeAsset root{};
            root.id = "driver_root";

            wz::engine::assets::SceneBehaviorAsset behavior{};
            behavior.module = wz::engine::behavior::kDriveForwardModule;
            behavior.name = wz::engine::behavior::kDriveForwardBehavior;
            behavior.enabled = true;
            behavior.events = { "frame.update" };
            behavior.config = {
                wz::engine::assets::SceneBehaviorConfigValue{
                    .key = wz::engine::behavior::kDriveForwardSpeedConfigKey,
                    .kind =
                        wz::engine::assets::SceneBehaviorConfigValueKind::Number,
                    .number_value = 8.0,
                },
                wz::engine::assets::SceneBehaviorConfigValue{
                    .key = wz::engine::behavior::kDriveForwardTurnRateConfigKey,
                    .kind =
                        wz::engine::assets::SceneBehaviorConfigValueKind::Number,
                    .number_value = 0.0,
                },
            };
            root.behavior = behavior;
            root.motion = wz::engine::assets::SceneMotionAsset{};

            return { root };
        }

        // A 2-node DRAWABLE prefab exercising BOTH render-binding routes the tank
        // prefabs use, against the fixture graph's pull-mesh chain: the root binds
        // geometry (graph node 9) + program (node 10) so ASSEMBLE synthesizes its
        // renderable, and an fx child points at a PRE-BUILT renderable (node 8)
        // resolved only by the renderable-key bridge. A spawn must materialize BOTH,
        // which the incremental spawn path (#252, A1) has to do itself now that the
        // full-scene trailing rematerialize is gone -- the pre-built child is the
        // muzzle_beam/projectile class that would go dark if the bridge were dropped.
        static std::vector<wz::engine::assets::SceneNodeAsset> make_drawable()
        {
            wz::engine::assets::SceneNodeAsset root{};
            root.id = "drawable_root";
            root.geometry_asset_node_id =
                static_cast<wz::asset::AssetGraphDraftNodeId>(9);
            root.render_program_node_id =
                static_cast<wz::asset::AssetGraphDraftNodeId>(10);

            wz::engine::assets::SceneNodeAsset fx{};
            fx.id = "drawable_fx";
            fx.parent_id = "drawable_root";
            fx.renderable_asset_node_id =
                static_cast<wz::asset::AssetGraphDraftNodeId>(8);

            return { root, fx };
        }
    };
}

// A structural rebuild (add_child) must PRESERVE a pre-existing binding's
// instance state: the accumulator's Y keeps climbing across the rebuild rather
// than resetting, and a freshly added binding initializes fresh.
TEST_F(WozzitsAppPrefabFixture, RebuildPreservesExistingBehaviorState)
{
    wz::app::WozzitsApp_v1 app(ctx);
    ASSERT_TRUE(app.load_scene(scene_load_desc()));

    // accumulate_on_frame writes Y = the number of frames it has dispatched, as
    // long as its instance-state counter survives. Two ticks -> Y == 2.
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    const auto after_two = app.node_local_translation("accumulator");
    ASSERT_TRUE(after_two.has_value());
    EXPECT_FLOAT_EQ(after_two->y, 2.0f);

    // A structural edit rebuilds the behavior runtime. If state did NOT survive,
    // accumulate_init would reconstruct counter=0 and the next tick's Y would be 1.
    const auto added = app.add_child_node("root");
    ASSERT_TRUE(added.ok) << added.error;

    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    const auto after_rebuild = app.node_local_translation("accumulator");
    ASSERT_TRUE(after_rebuild.has_value());
    EXPECT_FLOAT_EQ(after_rebuild->y, 3.0f)
        << "instance state did not survive the rebuild (counter reset to 0)";
}

// SPAWN_PREFAB must graft the registered prefab's subtree into the running
// scene; its behavior initializes and runs the next tick, and the spawner's own
// state is untouched.
TEST_F(WozzitsAppPrefabFixture, SpawnPrefabGraftsSubtreeAndRunsItsBehavior)
{
    wz::app::WozzitsApp_v1 app(ctx);
    app.register_prefab("spawnling", make_spawnling());
    ASSERT_TRUE(app.load_scene(scene_load_desc()));

    EXPECT_EQ(app.spawned_prefab_node_count(), 0u);

    // Tick 1: the spawner emits SPAWN_PREFAB; the frame-boundary drain grafts the
    // prefab + rebuilds. The accumulator advances to Y=1 this same tick.
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    EXPECT_EQ(app.spawned_prefab_node_count(), 1u)
        << "prefab subtree was not grafted into scene_nodes_";
    const auto accumulator_one = app.node_local_translation("accumulator");
    ASSERT_TRUE(accumulator_one.has_value());
    EXPECT_FLOAT_EQ(accumulator_one->y, 1.0f);

    // The spawned node exists with the minted id and sits at the spawn transform
    // (spawner world (100,0,0) × offset (0,0,5) = (100,0,5)).
    const auto spawned_pos =
        app.node_local_translation("spawn:1:spawnling_root");
    ASSERT_TRUE(spawned_pos.has_value())
        << "spawned node 'spawn:1:spawnling_root' missing from scene";
    EXPECT_FLOAT_EQ(spawned_pos->x, 100.0f);
    EXPECT_FLOAT_EQ(spawned_pos->z, 5.0f);

    // Tick 2: the spawned subtree's behavior (its OWN fresh instance state) runs,
    // moving its Y to 1; the pre-existing accumulator advances to 2 (its state was
    // untouched by the spawn-induced rebuild). The spawner spawns again -> a
    // second instance appears.
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    const auto spawned_after = app.node_local_translation("spawn:1:spawnling_root");
    ASSERT_TRUE(spawned_after.has_value());
    EXPECT_FLOAT_EQ(spawned_after->y, 1.0f)
        << "spawned subtree's behavior did not initialize + run";
    const auto accumulator_two = app.node_local_translation("accumulator");
    ASSERT_TRUE(accumulator_two.has_value());
    EXPECT_FLOAT_EQ(accumulator_two->y, 2.0f)
        << "pre-existing entity state was disturbed by the spawn";
    EXPECT_EQ(app.spawned_prefab_node_count(), 2u)
        << "a second spawn must mint a disjoint instance";
    EXPECT_TRUE(app.node_local_translation("spawn:2:spawnling_root").has_value());
}

// Scenelets under <resource_root>/scenelets/ are auto-registered as prefabs on
// load (the runtime-spawning glue): the fixture ships scenelets/spawnling.scene.-
// json, so the "spawnling" prefab exists WITHOUT the test calling register_prefab.
// The fixture's spawner emits SPAWN_PREFAB for "spawnling"; on the first tick it
// grafts the auto-registered scenelet's node into the running scene.
TEST_F(WozzitsAppPrefabFixture, AutoRegistersSceneletsAsPrefabsOnLoad)
{
    wz::app::WozzitsApp_v1 app(ctx);
    // No app.register_prefab(...) here — auto-registration from the scenelets
    // folder is the only source of the "spawnling" prefab.
    ASSERT_TRUE(app.load_scene(scene_load_desc()));

    // The scenelet catalog (backing the editor's scenelet menu) enumerates the
    // auto-registered prefab with its name + resource-relative scene-file path.
    const auto catalog = app.scenelet_catalog();
    ASSERT_EQ(catalog.size(), 1u);
    EXPECT_EQ(catalog[0].name, "spawnling");
    EXPECT_NE(
        catalog[0].path.find("spawnling.scene.json"), std::string::npos);

    EXPECT_EQ(app.spawned_prefab_node_count(), 0u);

    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    EXPECT_EQ(app.spawned_prefab_node_count(), 1u)
        << "auto-registered scenelet prefab was not grafted on spawn";

    // The spawned node carries the scenelet root's id under the spawn prefix and
    // sits at the spawner world (100,0,0) × offset (0,0,5) = (100,0,5).
    const auto spawned_pos =
        app.node_local_translation("spawn:1:spawnling_root");
    ASSERT_TRUE(spawned_pos.has_value())
        << "spawned node 'spawn:1:spawnling_root' missing from scene";
    EXPECT_FLOAT_EQ(spawned_pos->x, 100.0f);
    EXPECT_FLOAT_EQ(spawned_pos->z, 5.0f);
}

// Prefab-editor round-trip (seam 2): open a scenelet AS the working scene, then
// switch back to the main scene -- both reuse the same project asset graph.
TEST_F(WozzitsAppPrefabFixture, OpenSceneSwapsWorkingSceneAndBack)
{
    wz::app::WozzitsApp_v1 app(ctx);
    const auto desc = scene_load_desc();
    ASSERT_TRUE(app.load_scene(desc));

    // Main scene is loaded: its authored node exists, the scenelet's does not.
    EXPECT_TRUE(app.node_local_translation("spawner").has_value());
    EXPECT_FALSE(app.node_local_translation("spawnling_root").has_value());

    // Open the scenelet as the working scene (the prefab editor's "open").
    const auto catalog = app.scenelet_catalog();
    ASSERT_EQ(catalog.size(), 1u);
    ASSERT_TRUE(app.open_scene(catalog[0].path));
    EXPECT_TRUE(app.node_local_translation("spawnling_root").has_value());
    EXPECT_FALSE(app.node_local_translation("spawner").has_value());

    // authored_scene_nodes() (what wz_host_runtime_scene_snapshot serializes so the
    // editor rebuilds its tree) now reports the SCENELET's authored node, not the
    // main scene's.
    const auto has_node = [](const auto& nodes, std::string_view id) {
        for (const auto& n : nodes) {
            if (n.id == id) {
                return true;
            }
        }
        return false;
    };
    const auto swapped_nodes = app.authored_scene_nodes();
    EXPECT_TRUE(has_node(swapped_nodes, "spawnling_root"));
    EXPECT_FALSE(has_node(swapped_nodes, "spawner"));

    // Switch back to the main scene -- an empty path reopens it (no need to track).
    ASSERT_TRUE(app.open_scene({}));
    EXPECT_TRUE(app.node_local_translation("spawner").has_value());
    EXPECT_FALSE(app.node_local_translation("spawnling_root").has_value());
}

// A spawn while the scene camera is active (play mode) must NOT flip the active
// camera off its anchor (#219). The fixture's scene_camera behavior selects "cam"
// on load; after a spawn-induced rebuild the anchor is unchanged.
TEST_F(WozzitsAppPrefabFixture, SpawnDoesNotFlipActiveSceneCamera)
{
    wz::app::WozzitsApp_v1 app(ctx);
    app.register_prefab("spawnling", make_spawnling());
    app.set_prefer_scene_camera(true);  // play-mode policy: scene camera active
    ASSERT_TRUE(app.load_scene(scene_load_desc()));

    // The scene_camera behavior selected the "cam" node on scene.loaded.
    ASSERT_EQ(app.active_scene_camera_id(), "cam")
        << "scene camera was not selected on load";

    // One tick spawns a prefab (rebuilds the behavior runtime mid-tick). The
    // active camera anchor must remain "cam" — no flip to free-fly / the origin.
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    EXPECT_EQ(app.spawned_prefab_node_count(), 1u);
    EXPECT_EQ(app.active_scene_camera_id(), "cam")
        << "the spawn-induced rebuild flipped the active scene camera (#219)";
}

// End to end "spawn -> drives": a spawned prefab carrying the built-in
// drive_forward behavior advances its own position over successive ticks after
// being grafted. Registering make_driver() under the name the fixture's spawner
// emits ("spawnling") makes the spawner graft the self-driving prefab. We then
// watch the spawned root's position climb along its facing (+Z) tick over tick —
// the proof that runtime spawning produces something that actually drives.
TEST_F(WozzitsAppPrefabFixture, SpawnedPrefabDrivesItselfOverTicks)
{
    wz::app::WozzitsApp_v1 app(ctx);
    ASSERT_TRUE(app.load_scene(scene_load_desc()));
    // Register AFTER load so this overrides the scenelet folder's auto-registered
    // "spawnling" prefab — the fixture spawner emits "spawnling", and we want it to
    // graft our self-driving driver prefab (root id driver_root) instead.
    app.register_prefab("spawnling", make_driver());

    // Tick 1 grafts the driver prefab at the spawner world (100,0,0) × offset
    // (0,0,5) = (100,0,5). Its behavior runs starting the next tick.
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    ASSERT_EQ(app.spawned_prefab_node_count(), 1u);
    const auto start = app.node_local_translation("spawn:1:driver_root");
    ASSERT_TRUE(start.has_value())
        << "spawned driver node 'spawn:1:driver_root' missing from scene";
    EXPECT_FLOAT_EQ(start->x, 100.0f);
    EXPECT_FLOAT_EQ(start->z, 5.0f);

    // Each subsequent tick, drive_forward sets the spawned node's local +Z
    // velocity and the motion integrator advances it: Z strictly climbs. (A
    // second instance also spawns each tick, but we only watch the first.)
    float last_z = start->z;
    for (uint32_t tick = 0; tick < 3u; ++tick) {
        app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
        const auto pos = app.node_local_translation("spawn:1:driver_root");
        ASSERT_TRUE(pos.has_value());
        EXPECT_GT(pos->z, last_z)
            << "spawned drive_forward prefab did not advance on tick " << tick;
        last_z = pos->z;
    }
    EXPECT_GT(last_z, start->z)
        << "spawned prefab carrying drive_forward never moved";
}

// A spawned DRAWABLE prefab must materialize its render bindings INCREMENTALLY
// (#252, A1): the spawn path bridges + assembles + commit/resolves only the
// spawned subtree instead of re-materializing the whole scene, and BOTH the
// assembled-geometry root and the pre-built-renderable child come up with a
// resolved renderable key. The pre-built child is the muzzle_beam/projectile
// class that regressed when the full-scene trailing rematerialize was removed --
// this test fails (fx has no key) if the spawn path forgets to bridge pre-built
// renderables on the spawned span.
TEST_F(WozzitsAppPrefabFixture, SpawnMaterializesDrawableSubtree)
{
    wz::app::WozzitsApp_v1 app(ctx);
    ASSERT_TRUE(app.load_scene(scene_load_desc()));
    // Register AFTER load so this overrides the scenelet folder's auto-registered
    // (1-node, behavior-only) "spawnling" -- the fixture spawner emits "spawnling",
    // and we want it to graft our 2-node DRAWABLE prefab instead.
    app.register_prefab("spawnling", make_drawable());

    // The base scene draws nothing (behavior + camera nodes only), even though the
    // graph now carries a pull-mesh render chain.
    const std::size_t drawables_before = app.resolved_renderable_node_count();
    EXPECT_EQ(drawables_before, 0u);

    // Tick 1 spawns instance 1 (the 2-node drawable prefab). No scene_source, so no
    // grafted children -- exactly the 2 authored prefab nodes.
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    ASSERT_EQ(app.spawned_prefab_node_count(), 2u)
        << "the 2-node drawable prefab was not grafted";

    // Both render-binding routes materialized for the spawned subtree:
    //   - the root's renderable was ASSEMBLED from geometry + program, and
    //   - the fx child's PRE-BUILT renderable was resolved by the key bridge.
    const auto root_key = app.node_renderable_asset("spawn:1:drawable_root");
    const auto fx_key = app.node_renderable_asset("spawn:1:drawable_fx");
    EXPECT_TRUE(root_key.has_value())
        << "spawned geometry+program node did not assemble a renderable";
    EXPECT_TRUE(fx_key.has_value())
        << "spawned pre-built renderable was not bridged (the muzzle_beam / "
           "projectile regression if the spawn drops the renderable-key bridge)";
    EXPECT_EQ(app.resolved_renderable_node_count(), drawables_before + 2u);

    // Incremental: a second spawn materializes its OWN subtree and leaves the first
    // instance's resolved key untouched (content-keyed, so equality also holds).
    app.simulation_tick(wz::input::InputState{}, 1.0f / 60.0f);
    ASSERT_EQ(app.spawned_prefab_node_count(), 4u);
    EXPECT_TRUE(app.node_renderable_asset("spawn:2:drawable_root").has_value());
    EXPECT_TRUE(app.node_renderable_asset("spawn:2:drawable_fx").has_value());
    const auto root_key_after =
        app.node_renderable_asset("spawn:1:drawable_root");
    ASSERT_TRUE(root_key_after.has_value())
        << "the second spawn disturbed the first instance's renderable";
    EXPECT_EQ(*root_key_after, *root_key);
    EXPECT_EQ(app.resolved_renderable_node_count(), drawables_before + 4u);
}
