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
#include <engine/project/project_manifest.h>

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
