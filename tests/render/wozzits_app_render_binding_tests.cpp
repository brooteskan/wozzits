// tests/render/wozzits_app_render_binding_tests.cpp
//
// On-device coverage for issue #213 increment 2: live authoring of a scene
// node's renderable BINDING (geometry + render program) via WozzitsApp_v1's
// set_node_geometry_asset / set_node_render_program. Reuses test_rebind_fixture's
// asset graph (node 9 = gpu_sparse_mesh geometry, node 10 = render program) and
// render_binding.scene.json (solo carries its own program; inherited carries
// only geometry and inherits the program from its parent group).
//
// The test exercises the live apply + re-assembly + the program-inheritance
// re-evaluation through resolved_renderable_node_count() (the count of scene
// nodes that resolved a renderable key). On-device: WozzitsApp_v1 owns a
// RhiSceneRenderer that needs a DX12 device; skipped if none can be created.

#include <gtest/gtest.h>

#include <engine/app/wozzits_app_v1.h>
#include <engine/app_context.h>
#include <engine/project/project_manifest.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>

namespace
{
    constexpr const char* kProjectRoot = "projects/test_rebind_fixture";

    struct WozzitsAppRenderBindingFixture : public ::testing::Test
    {
        wz::engine::AppContext ctx;
        bool initialized = false;

        void SetUp() override
        {
            wz::engine::AppDesc desc;
            desc.window = {
                "wozzits_app_v1_render_binding_test", 256, 256, false, false };
            desc.resource_root = "resources";
            initialized = wz::engine::init(ctx, desc);
            if (!initialized) {
                GTEST_SKIP()
                    << "no GPU device — skipping on-device render-binding test";
            }
        }

        void TearDown() override
        {
            if (initialized) {
                wz::engine::shutdown(ctx);
            }
        }

        // Load test_rebind_fixture's graph paired with the binding scene that
        // sits beside it (render_binding.scene.json), not the fixture's own
        // pre-built-renderable scene.
        wz::app::WozzitsAppSceneLoadDesc binding_load_desc() const
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
                "render_binding.scene.json");
            return load_desc;
        }
    };
}

// Authoring a node's geometry/program binding live re-assembles its renderable,
// and editing an ancestor's program cascades to inheriting descendants.
TEST_F(WozzitsAppRenderBindingFixture, SetNodeBindingLiveReassembles)
{
    wz::app::WozzitsApp_v1 app(ctx);
    ASSERT_TRUE(app.load_scene(binding_load_desc()));

    // Both bound nodes assemble: solo (own program) + inherited (program
    // inherited from group).
    EXPECT_EQ(app.resolved_renderable_node_count(), 2u);

    // Clear solo's geometry -> it stops drawing.
    EXPECT_TRUE(app.set_node_geometry_asset("solo", 0));
    EXPECT_EQ(app.resolved_renderable_node_count(), 1u);

    // Re-add solo's geometry (graph node 9) -> draws again.
    EXPECT_TRUE(app.set_node_geometry_asset(
        "solo", static_cast<wz::asset::AssetGraphDraftNodeId>(9)));
    EXPECT_EQ(app.resolved_renderable_node_count(), 2u);

    // Clear the group's render program -> inherited loses its (inherited)
    // program and stops drawing; solo (own program) is unaffected.
    EXPECT_TRUE(app.set_node_render_program("group", 0));
    EXPECT_EQ(app.resolved_renderable_node_count(), 1u);

    // Give inherited its own program (graph node 10) -> draws again.
    EXPECT_TRUE(app.set_node_render_program(
        "inherited", static_cast<wz::asset::AssetGraphDraftNodeId>(10)));
    EXPECT_EQ(app.resolved_renderable_node_count(), 2u);

    // A missing node is a logged no-op, not a crash.
    EXPECT_FALSE(app.set_node_geometry_asset(
        "no_such_node", static_cast<wz::asset::AssetGraphDraftNodeId>(9)));
}

// #221 no-sim path: this scene has no behaviors/motion/terrain/constraints, so
// there is no live behavior_scene_. scene_world_transforms() must then be
// exactly the authored composition (compute_scene_node_world_transforms) — same
// hierarchical result the renderer used to compute internally. 'inherited' sits
// at local origin under 'group' (translation +150 x), so its world X is 150;
// 'solo' is a root at -150. Proves the fallback composes the parent chain.
TEST_F(WozzitsAppRenderBindingFixture, NoSimSceneWorldTransformsAreAuthoredComposition)
{
    wz::app::WozzitsApp_v1 app(ctx);
    ASSERT_TRUE(app.load_scene(binding_load_desc()));

    // No simulation was materialized for this static scene.
    ASSERT_EQ(app.active_behavior_binding_count(), 0u);

    // Root 'solo' draws at its own local translation.
    const std::optional<wz::math::Mat4> solo = app.node_world_transform("solo");
    ASSERT_TRUE(solo.has_value());
    EXPECT_FLOAT_EQ(solo->m[12], -150.0f);  // world-space X translation

    // 'inherited' (local origin) inherits 'group's +150 X — the parent-chain
    // composition the standalone compute_scene_node_world_transforms produces.
    const std::optional<wz::math::Mat4> inherited =
        app.node_world_transform("inherited");
    ASSERT_TRUE(inherited.has_value());
    EXPECT_FLOAT_EQ(inherited->m[12], 150.0f);
}
