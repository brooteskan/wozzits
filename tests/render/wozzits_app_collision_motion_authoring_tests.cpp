// tests/render/wozzits_app_collision_motion_authoring_tests.cpp
//
// On-device coverage for issue #216/#217: live authoring of a scene node's
// Collision component by REFERENCE (set_node_collision_asset) and of a node's
// Motion terrain-stick fields (set_node_motion_terrain_fields) via WozzitsApp_v1,
// plus their persistence through save_scene + reload.
//
// Reuses test_rebind_fixture's asset graph (node 9 + node 10 are committed graph
// nodes with real keys) paired with collision_ref.scene.json (a 'landscape' node
// + a 'tank' node). The collision-reference bridge (bridge_scene_collision_keys)
// is asset-type-AGNOSTIC: it resolves a graph node id to that node's committed
// key, so node 9 stands in for an authored collision_from_height_field node here.
// The test asserts the verb + bridge + persistence end to end; the collision
// asset's heightfield contents are covered device-free by the scene roundtrip and
// the collision asset/frame tests.
//
// On-device: WozzitsApp_v1 owns a RhiSceneRenderer that needs a DX12 device;
// GTEST_SKIP if none can be created.

#include <gtest/gtest.h>

#include <engine/app/wozzits_app_v1.h>
#include <engine/app_context.h>
#include <engine/project/project_manifest.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>

namespace
{
    constexpr const char* kProjectRoot = "projects/test_rebind_fixture";

    // From test_rebind_fixture's graph: a committed node whose key the collision
    // reference bridges to (its asset type is irrelevant to the bridge).
    constexpr wz::asset::AssetGraphDraftNodeId kCollisionGraphNode = 9u;

    wz::app::WozzitsAppSceneLoadDesc collision_load_desc(
        const wz::engine::AppContext& ctx)
    {
        const auto project = wz::engine::project::load_project_manifest(
            wz::engine::project::ProjectManifestLoadDesc{
                .project_root = kProjectRoot,
                .resource_root = ctx.assets->resource_root(),
            });
        EXPECT_TRUE(project.ok) << project.error;

        wz::app::WozzitsAppSceneLoadDesc load_desc{};
        load_desc.asset_graph = project.manifest.asset_graph_path;
        load_desc.scene = wz::fs::join(
            wz::fs::parent_path(project.manifest.scene_path),
            "collision_ref.scene.json");
        return load_desc;
    }

    struct WozzitsAppCollisionMotionFixture : public ::testing::Test
    {
        wz::engine::AppContext ctx;
        bool initialized = false;

        void SetUp() override
        {
            wz::engine::AppDesc desc;
            desc.window = {
                "wozzits_app_v1_collision_motion_test", 256, 256, false, false };
            desc.resource_root = "resources";
            initialized = wz::engine::init(ctx, desc);
            if (!initialized) {
                GTEST_SKIP()
                    << "no GPU device — skipping on-device collision/motion "
                       "authoring test";
            }
        }

        void TearDown() override
        {
            if (initialized) {
                wz::engine::shutdown(ctx);
            }
        }
    };
}

// Authoring a node's collision reference bridges to the graph node's key and sets
// constrain_movement; authoring the motion terrain-stick fields sets them; both
// persist through save_scene + a fresh-AppContext reload.
TEST_F(WozzitsAppCollisionMotionFixture, AuthorAndPersistCollisionRefAndMotion)
{
    {
        wz::app::WozzitsApp_v1 app(ctx);
        ASSERT_TRUE(app.load_scene(collision_load_desc(ctx)));

        // NOTE: save_scene below persists into the staged scene file, so the
        // landscape/tank may already carry the authored components on a re-run.
        // The test asserts the post-authoring + post-reload state, which holds
        // regardless of the starting state (the verbs are idempotent setters).

        // Author the collision by REFERENCE: point landscape at graph node 9 and
        // constrain movement. The bridge must resolve node 9's committed key.
        EXPECT_TRUE(app.set_node_collision_asset(
            "landscape", kCollisionGraphNode, /*constrain_movement=*/true));

        const auto* collision = app.node_collision("landscape");
        ASSERT_NE(collision, nullptr);
        ASSERT_TRUE(collision->collision_asset_node_id.has_value());
        EXPECT_EQ(*collision->collision_asset_node_id, kCollisionGraphNode);
        EXPECT_TRUE(collision->constrain_movement);
        EXPECT_NE(collision->collision_asset, wz::asset::AssetKey{})
            << "collision reference did not bridge to the graph node's key";

        // Author the tank's Motion terrain-stick fields (creating the Motion
        // component if the tank has none yet).
        EXPECT_TRUE(app.set_node_motion_terrain_fields(
            "tank",
            /*terrain_constrained=*/true,
            /*ride_height=*/1.25f,
            /*footprint_radius=*/2.5f,
            /*align_to_surface=*/true,
            /*alignment_strength=*/0.75f));

        const auto* motion = app.node_motion("tank");
        ASSERT_NE(motion, nullptr);
        EXPECT_TRUE(motion->terrain_constrained);
        EXPECT_FLOAT_EQ(motion->terrain_ride_height, 1.25f);
        EXPECT_FLOAT_EQ(motion->terrain_footprint_radius, 2.5f);
        EXPECT_TRUE(motion->terrain_align_to_surface);
        EXPECT_FLOAT_EQ(motion->terrain_alignment_strength, 0.75f);

        // A missing node is a logged no-op, not a crash.
        EXPECT_FALSE(app.set_node_collision_asset("no_such_node", 9u, true));
        EXPECT_FALSE(app.set_node_motion_terrain_fields(
            "no_such_node", true, 0.0f, 0.0f, false, 1.0f));

        ASSERT_TRUE(app.save_scene());
    }

    // Reload in a FRESH AppContext (a new asset cache, so the by-path compiled
    // scene cache does not shadow the just-saved file) and assert the authored
    // intent persisted: the collision NODE-REF + constrain_movement, and the
    // motion terrain fields.
    wz::engine::AppContext reload_ctx;
    wz::engine::AppDesc reload_desc;
    reload_desc.window = {
        "wozzits_app_v1_collision_motion_reload", 256, 256, false, false };
    reload_desc.resource_root = "resources";
    ASSERT_TRUE(wz::engine::init(reload_ctx, reload_desc));

    {
        wz::app::WozzitsApp_v1 reloaded(reload_ctx);
        ASSERT_TRUE(reloaded.load_scene(collision_load_desc(reload_ctx)));

        const auto* collision = reloaded.node_collision("landscape");
        ASSERT_NE(collision, nullptr);
        ASSERT_TRUE(collision->collision_asset_node_id.has_value());
        EXPECT_EQ(*collision->collision_asset_node_id, kCollisionGraphNode);
        EXPECT_TRUE(collision->constrain_movement);
        // The key is re-bridged from the graph on load (not read from JSON).
        EXPECT_NE(collision->collision_asset, wz::asset::AssetKey{});

        const auto* motion = reloaded.node_motion("tank");
        ASSERT_NE(motion, nullptr);
        EXPECT_TRUE(motion->terrain_constrained);
        EXPECT_FLOAT_EQ(motion->terrain_ride_height, 1.25f);
        EXPECT_FLOAT_EQ(motion->terrain_footprint_radius, 2.5f);
        EXPECT_TRUE(motion->terrain_align_to_surface);
        EXPECT_FLOAT_EQ(motion->terrain_alignment_strength, 0.75f);
    }

    wz::engine::shutdown(reload_ctx);
}
