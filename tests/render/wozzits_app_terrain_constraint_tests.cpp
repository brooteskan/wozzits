// tests/render/wozzits_app_terrain_constraint_tests.cpp
//
// On-device coverage for issue #216 work item B: WozzitsApp_v1's tick now runs
// the FULL constraint pipeline (build_collision_frame -> [behaviors] ->
// integrate_motion -> apply_terrain_constraints), and runs it even when the
// scene has NO behaviors. A terrain_constrained Motion actor must therefore
// stick to the authored heightfield constraint surface after a simulation tick,
// with zero behaviors in the scene.
//
// The fixture (terrain_constraint_fixture) is fully portable: an empty asset
// graph + a scene with a heightfield terrain whose height field is an inline
// PROCEDURAL scalar field (radial gradient, no external file), a
// constrain_movement constraint surface (calculate_constraint_surface=true),
// and two Motion actors started ABOVE the surface at the field center:
//   - "actor"   terrain_constrained=true  -> snaps down to surface + ride height
//   - "control" terrain_constrained=false -> does not move (proves the snap is
//                                            gated on the constraint flag, not a
//                                            blanket gravity)
//
// The radial-gradient field is 0 at its center and 1 at its corners, mapped to
// world Y = value*vertical_scale + base_height. Both actors sit at world XZ
// (0,0) = the field center, so the surface there is base_height (= 2.0) and the
// snap target is base_height + ride_height (= 2.5). The field is bounded in
// [base_height, base_height + vertical_scale] = [2, 12], so we assert the
// snapped Y is in that band + ride height and far below the start height; the
// exact center value depends on the projection-resolution sampling, so we bound
// rather than demand a single float. We also tick twice and assert the stuck Y
// is stable (idempotent stick).
//
// On-device: WozzitsApp_v1 owns a RhiSceneRenderer that needs a DX12 device;
// GTEST_SKIP if none can be created.

#include <gtest/gtest.h>

#include <engine/app/wozzits_app_v1.h>
#include <engine/app_context.h>
#include <engine/project/project_manifest.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <input/input.h>

#include <cmath>
#include <fstream>
#include <iterator>

namespace
{
    constexpr const char* kProjectRoot = "projects/terrain_constraint_fixture";

    // From the fixture's terrain_height_field_source + actor authoring.
    constexpr float kBaseHeight = 2.0f;
    constexpr float kVerticalScale = 10.0f;
    constexpr float kRideHeight = 0.5f;
    constexpr float kStartHeight = 50.0f;

    struct WozzitsAppTerrainConstraintFixture : public ::testing::Test
    {
        wz::engine::AppContext ctx;
        bool initialized = false;

        void SetUp() override
        {
            wz::engine::AppDesc desc;
            desc.window = {
                "wozzits_app_v1_terrain_constraint_test", 256, 256, false,
                false };
            desc.resource_root = "resources";
            initialized = wz::engine::init(ctx, desc);
            if (!initialized) {
                GTEST_SKIP()
                    << "no GPU device — skipping on-device terrain-constraint "
                       "test";
            }
        }

        void TearDown() override
        {
            if (initialized) {
                wz::engine::shutdown(ctx);
            }
        }

        wz::app::WozzitsAppSceneLoadDesc load_desc() const
        {
            const auto project = wz::engine::project::load_project_manifest(
                wz::engine::project::ProjectManifestLoadDesc{
                    .project_root = kProjectRoot,
                    .resource_root = ctx.assets->resource_root(),
                });
            EXPECT_TRUE(project.ok) << project.error;

            wz::app::WozzitsAppSceneLoadDesc desc{};
            desc.asset_graph = project.manifest.asset_graph_path;
            desc.scene = project.manifest.scene_path;
            desc.behavior_module_folder =
                project.manifest.behavior_module_folder;
            return desc;
        }
    };
}

// A terrain_constrained Motion actor sticks to the heightfield constraint
// surface after a tick, even though the scene has NO behaviors — proving the
// tick runs build_collision_frame + apply_terrain_constraints unconditionally.
TEST_F(WozzitsAppTerrainConstraintFixture, ConstrainedActorSticksWithNoBehaviors)
{
    wz::app::WozzitsApp_v1 app(ctx);
    ASSERT_TRUE(app.load_scene(load_desc()));

    // The scene carries zero behaviors: the simulation must still run.
    EXPECT_EQ(app.active_behavior_binding_count(), 0u);

    const auto actor_start = app.node_local_translation("actor");
    const auto control_start = app.node_local_translation("control");
    ASSERT_TRUE(actor_start.has_value());
    ASSERT_TRUE(control_start.has_value());
    EXPECT_NEAR(actor_start->y, kStartHeight, 1e-3f);
    EXPECT_NEAR(control_start->y, kStartHeight, 1e-3f);

    const wz::input::InputState input{};
    app.simulation_tick(input, 1.0f / 60.0f);

    const auto actor_after = app.node_local_translation("actor");
    const auto control_after = app.node_local_translation("control");
    ASSERT_TRUE(actor_after.has_value());
    ASSERT_TRUE(control_after.has_value());

    // The constrained actor snapped down to the surface: far below its start,
    // and within the field's world-Y band [base_height, base_height+scale] plus
    // its ride height. (Radial gradient center = base_height = 2.0, so the snap
    // target is ~2.5; corners would be ~12.5. We bound rather than fix the
    // float because the exact center sample depends on projection resolution.)
    EXPECT_LT(actor_after->y, kStartHeight - 1.0f)
        << "constrained actor did not snap down to the surface";
    EXPECT_GE(actor_after->y, kBaseHeight + kRideHeight - 1e-2f);
    EXPECT_LE(actor_after->y, kBaseHeight + kVerticalScale + kRideHeight + 1e-2f);

    // The actor's XZ must be unchanged by the constraint (only Y is snapped).
    EXPECT_NEAR(actor_after->x, actor_start->x, 1e-3f);
    EXPECT_NEAR(actor_after->z, actor_start->z, 1e-3f);

    // The control (terrain_constrained=false) must NOT move: the snap is gated
    // on the per-actor constraint flag, not applied to every motion.
    EXPECT_NEAR(control_after->y, kStartHeight, 1e-3f)
        << "unconstrained control actor was moved by the constraint pass";

    // Sticking is idempotent: a second tick leaves the actor on the surface.
    const float settled_y = actor_after->y;
    app.simulation_tick(input, 1.0f / 60.0f);
    const auto actor_settled = app.node_local_translation("actor");
    ASSERT_TRUE(actor_settled.has_value());
    EXPECT_NEAR(actor_settled->y, settled_y, 1e-3f)
        << "constrained actor did not stay stuck on a second tick";
}

// #218 follow-up: editing a sim-driven node's SCALE (a DOF the terrain
// constraint does not control) must survive the per-frame write-back. The edit
// goes to scene_nodes_; the sim runs on a separate behavior_scene_ copy and
// writes its full TRS back into scene_nodes_ each tick. set_node_transform now
// mirrors the edit into the sim node, so the write-back preserves the authored
// scale instead of reverting it to the sim node's stale value (~1). Without the
// sync this asserts at scale ~1.
TEST_F(WozzitsAppTerrainConstraintFixture, ConstrainedActorKeepsEditedScaleAcrossTick)
{
    wz::app::WozzitsApp_v1 app(ctx);
    ASSERT_TRUE(app.load_scene(load_desc()));

    const auto start_t = app.node_local_translation("actor");
    ASSERT_TRUE(start_t.has_value());

    // Scale the constrained actor up, keeping its translation; rotation identity
    // (the align-to-surface constraint will set rotation itself each frame).
    wz::engine::assets::AuthoredTransform edited{};
    edited.translation[0] = start_t->x;
    edited.translation[1] = start_t->y;
    edited.translation[2] = start_t->z;
    edited.rotation_quat[0] = 0.0f;
    edited.rotation_quat[1] = 0.0f;
    edited.rotation_quat[2] = 0.0f;
    edited.rotation_quat[3] = 1.0f;
    edited.scale[0] = 5.0f;
    edited.scale[1] = 5.0f;
    edited.scale[2] = 5.0f;
    ASSERT_TRUE(app.set_node_transform("actor", edited));

    const wz::input::InputState input{};
    app.simulation_tick(input, 1.0f / 60.0f);

    // The authored scale survives the constraint write-back.
    const auto after_s = app.node_local_scale("actor");
    ASSERT_TRUE(after_s.has_value());
    EXPECT_NEAR(after_s->x, 5.0f, 1e-3f)
        << "edited scale was reverted by the per-frame sim write-back";
    EXPECT_NEAR(after_s->y, 5.0f, 1e-3f);
    EXPECT_NEAR(after_s->z, 5.0f, 1e-3f);

    // And the actor still snaps to the surface — the scale edit didn't break the
    // constraint (Y is driven down well below the start height).
    const auto after_t = app.node_local_translation("actor");
    ASSERT_TRUE(after_t.has_value());
    EXPECT_LT(after_t->y, kStartHeight - 1.0f)
        << "constrained actor stopped sticking after a scale edit";
}

// #221: an editor-style set_node_transform edit is visible in the RENDER world
// transforms — the sim poke set_node_transform does is what makes the edit show
// up now that the renderer reads the polytree. Includes the 883d38d scale case:
// a terrain-constrained actor's scale (a DOF the constraint doesn't drive) must
// carry into the drawn world matrix.
TEST_F(WozzitsAppTerrainConstraintFixture, EditVisibleInRenderWorldTransform)
{
    wz::app::WozzitsApp_v1 app(ctx);
    ASSERT_TRUE(app.load_scene(load_desc()));

    const auto start_t = app.node_local_translation("actor");
    ASSERT_TRUE(start_t.has_value());

    // Edit: move the actor and scale it 3x. Keep rotation identity (the constraint
    // sets rotation each frame). The XZ move should show in the world transform.
    wz::engine::assets::AuthoredTransform edited{};
    edited.translation[0] = start_t->x + 7.0f;
    edited.translation[1] = start_t->y;
    edited.translation[2] = start_t->z - 4.0f;
    edited.rotation_quat[0] = 0.0f;
    edited.rotation_quat[1] = 0.0f;
    edited.rotation_quat[2] = 0.0f;
    edited.rotation_quat[3] = 1.0f;
    edited.scale[0] = 3.0f;
    edited.scale[1] = 3.0f;
    edited.scale[2] = 3.0f;
    ASSERT_TRUE(app.set_node_transform("actor", edited));

    const wz::input::InputState input{};
    app.simulation_tick(input, 1.0f / 60.0f);

    // The render world matrix reflects the edited translation (XZ) and scale. The
    // actor is a root, so its world == local. Column 3 = translation; the basis
    // column lengths = scale (the constraint keeps rotation orthonormal + scale).
    const std::optional<wz::math::Mat4> world = app.node_world_transform("actor");
    ASSERT_TRUE(world.has_value());
    EXPECT_NEAR(world->m[12], start_t->x + 7.0f, 1e-2f)
        << "edited X translation not visible in the render world transform";
    EXPECT_NEAR(world->m[14], start_t->z - 4.0f, 1e-2f)
        << "edited Z translation not visible in the render world transform";

    const float basis_x = std::sqrt(
        world->m[0] * world->m[0] + world->m[1] * world->m[1]
        + world->m[2] * world->m[2]);
    EXPECT_NEAR(basis_x, 3.0f, 1e-2f)
        << "edited scale not visible in the render world transform (883d38d)";
}

// #221 derive-on-save: after the sim moves an actor, save_scene persists the
// SIM-CURRENT pose (the write-back used to keep scene_nodes_ current; now the
// pose is decomposed from the polytree at save time). Reload proves the saved Y
// is the settled surface Y, not the authored start height.
TEST_F(WozzitsAppTerrainConstraintFixture, SaveScenePersistsSimCurrentPose)
{
    const auto project = wz::engine::project::load_project_manifest(
        wz::engine::project::ProjectManifestLoadDesc{
            .project_root = kProjectRoot,
            .resource_root = ctx.assets->resource_root(),
        });
    ASSERT_TRUE(project.ok) << project.error;

    // Save/reload through a TEMP scene file so the staged fixture is never
    // clobbered (procedural terrain resolves without external files, so an
    // absolute scene path elsewhere still loads).
    const wz::fs::Path staged_scene =
        wz::fs::join(ctx.assets->resource_root(), project.manifest.scene_path);
    std::string scene_text;
    {
        std::ifstream in(staged_scene, std::ios::binary);
        ASSERT_TRUE(in.good()) << "could not read staged fixture scene";
        scene_text.assign(
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>());
    }
    const wz::fs::Path temp_scene = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wozzits_derive_on_save_test.scene.json");
    {
        std::ofstream out(temp_scene, std::ios::binary);
        ASSERT_TRUE(out.good());
        out << scene_text;
    }

    wz::app::WozzitsAppSceneLoadDesc save_desc{};
    save_desc.asset_graph = project.manifest.asset_graph_path;
    save_desc.scene = temp_scene;  // absolute -> save_scene writes back here
    save_desc.behavior_module_folder = project.manifest.behavior_module_folder;

    float settled_y = 0.0f;
    {
        wz::app::WozzitsApp_v1 app(ctx);
        ASSERT_TRUE(app.load_scene(save_desc));

        // An authored edit marks the scene dirty so save_scene actually writes
        // (a pure sim tick no longer touches scene_nodes_, so it alone leaves the
        // scene clean). Keep the authored start Y — the point is that the SETTLED
        // sim Y is what gets persisted, not this authored Y.
        const auto start_t = app.node_local_translation("actor");
        ASSERT_TRUE(start_t.has_value());
        wz::engine::assets::AuthoredTransform edited{};
        edited.translation[0] = start_t->x + 1.0f;  // nudge -> scene_dirty_
        edited.translation[1] = start_t->y;          // still the authored start Y
        edited.translation[2] = start_t->z;
        edited.rotation_quat[3] = 1.0f;
        edited.scale[0] = 1.0f;
        edited.scale[1] = 1.0f;
        edited.scale[2] = 1.0f;
        ASSERT_TRUE(app.set_node_transform("actor", edited));

        const wz::input::InputState input{};
        app.simulation_tick(input, 1.0f / 60.0f);

        const auto after = app.node_local_translation("actor");
        ASSERT_TRUE(after.has_value());
        settled_y = after->y;
        // Sanity: the actor actually snapped (settled well below the start).
        ASSERT_LT(settled_y, kStartHeight - 1.0f);

        // The STORED authored transform still carries the authored/edit Y — the
        // sim did NOT write its settled pose back into scene_nodes_ (no write-back).
        const auto stored = app.stored_node_local_translation("actor");
        ASSERT_TRUE(stored.has_value());
        EXPECT_NEAR(stored->y, kStartHeight, 1e-3f)
            << "scene_nodes_ was mutated per frame — write-back should be gone";

        // save_scene must derive the sim-current pose from the polytree.
        ASSERT_EQ(app.save_scene(), wz::fs::FileError::None);
    }

    // Reload in a FRESH engine context so the scene is re-read from disk.
    wz::engine::AppContext reload_ctx;
    wz::engine::AppDesc reload_desc;
    reload_desc.window = {
        "wozzits_app_v1_derive_on_save_reload", 256, 256, false, false };
    reload_desc.resource_root = "resources";
    ASSERT_TRUE(wz::engine::init(reload_ctx, reload_desc));
    {
        wz::app::WozzitsApp_v1 app(reload_ctx);
        ASSERT_TRUE(app.load_scene(save_desc));

        // The reloaded authored Y is the settled surface Y saved above — proving
        // derive-on-save captured the sim pose (matching the old write-back's
        // saved result), not the authored start height.
        const auto reloaded = app.stored_node_local_translation("actor");
        ASSERT_TRUE(reloaded.has_value());
        EXPECT_NEAR(reloaded->y, settled_y, 1e-2f)
            << "save did not persist the sim-current pose (derive-on-save)";
    }
    wz::engine::shutdown(reload_ctx);
}
