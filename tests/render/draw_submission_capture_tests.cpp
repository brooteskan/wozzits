// tests/render/draw_submission_capture_tests.cpp
//
// A FRAME CAPTURE, for the two rows of the #327 rendering-capability-ladder
// register that rested on reading the submit loop rather than watching it run:
// Draw submission and Transparency.
//
// The register's method note says absences are inferred from absence of code
// and that "I found nothing" is weaker than "I found this". These tests turn
// four of its claims into measurements by recording, at the two places that
// can answer, what a real device frame actually does:
//
//   * RhiSceneRenderer::captured_submissions() -- WHAT the renderer handed to
//     record_packet, in submission order, with the node each came from.
//   * RhiDx12CommandRecorder::CapturedDraw -- the arguments of every
//     ID3D12GraphicsCommandList::DrawInstanced call, taken at the call site.
//
// Neither alone is enough. The first is the renderer's intent and could be a
// lie about the command list; the second is ground truth but anonymous. Read
// together, one is the control for the other, and the first test here is that
// cross-check rather than any claim about rungs. It has already earned that
// place once: a census over a real project came back 23 packets against 112
// draws, because the two logs had different lifetimes (the renderer cleared
// per render_scene while the recorder accumulated over a whole host frame,
// which is several passes). Hence begin_frame_capture / end_frame_capture,
// which scope both to the same span.
//
// The scene is alpha_order.scene.json: three MUTUALLY OVERLAPPING,
// ALPHA-BLENDED cubes strung along the view axis, authored near-first. If
// anything anywhere sorted transparent geometry back-to-front, this is the
// scene it would reverse.
//
// On-device: skipped when no GPU device can be created.

#include <gtest/gtest.h>

#include <engine/app/wozzits_app_v1.h>
#include <engine/app_context.h>
#include <engine/assets/renderable/renderable.h>
#include <engine/assets/scene/scene_asset_data.h>
#include <engine/project/project_manifest.h>
#include <engine/rendering/rhi_scene_renderer.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <input/input.h>
#include <math/mat4.h>
#include <math/math_types.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    namespace ea = wz::engine::assets;
    namespace er = wz::engine::rendering;

    constexpr const char* kProjectRoot = "projects/test_rebind_fixture";

    using CapturedDraw = er::RhiDx12CommandRecorder::CapturedDraw;

    struct DrawSubmissionCaptureFixture : public ::testing::Test
    {
        wz::engine::AppContext ctx;
        bool initialized = false;

        void SetUp() override
        {
            wz::engine::AppDesc desc;
            desc.window = {
                "wozzits_draw_submission_capture_test", 256, 256, false, false };
            desc.resource_root = "resources";
            initialized = wz::engine::init(ctx, desc);
            if (!initialized) {
                GTEST_SKIP()
                    << "no GPU device — skipping on-device draw capture";
            }
        }

        void TearDown() override
        {
            if (initialized) {
                wz::engine::shutdown(ctx);
            }
        }

        // The fixture graph paired with the three-alpha-cube scene.
        wz::app::WozzitsAppSceneLoadDesc alpha_order_load_desc() const
        {
            const auto project = wz::engine::project::load_project_manifest(
                wz::engine::project::ProjectManifestLoadDesc{
                    .project_root = kProjectRoot,
                    .resource_root = ctx.assets->resource_root(),
                });
            EXPECT_TRUE(project.ok) << project.error;

            return wz::app::WozzitsAppSceneLoadDesc{
                .asset_graph = project.manifest.asset_graph_path,
                .scene = wz::fs::join(
                    wz::fs::parent_path(project.manifest.scene_path),
                    "alpha_order.scene.json"),
            };
        }

        // begin/clear/render/end/present one frame through the app's own path.
        void render_one_app_frame(wz::app::WozzitsApp_v1& app)
        {
            ASSERT_TRUE(wz::gpu::begin_frame(ctx.device));
            wz::gpu::clear(ctx.device, 0.10f, 0.10f, 0.12f, 1.0f);
            app.simulation_tick(wz::input::InputState{}, 0.0f);
            EXPECT_TRUE(app.render_scene());
            ASSERT_TRUE(wz::gpu::end_frame(ctx.device));
            wz::gpu::present(ctx.device, /*sync_interval*/ 0);
        }

        // One frame recorded from a node span WE compose, through the app's
        // already-warm renderer, so a viewpoint can change without touching
        // scene authoring.
        void render_one_frame_of(
            wz::app::WozzitsApp_v1& app,
            const std::vector<ea::SceneNodeAsset>& nodes,
            const wz::math::Mat4& view_projection,
            const wz::math::Vec3& camera_world_pos)
        {
            app.renderer_for_testing().begin_frame_capture();
            ASSERT_TRUE(wz::gpu::begin_frame(ctx.device));
            wz::gpu::clear(ctx.device, 0.10f, 0.10f, 0.12f, 1.0f);
            EXPECT_TRUE(app.renderer_for_testing().render_scene(
                nodes, *ctx.assets, view_projection, camera_world_pos));
            ASSERT_TRUE(wz::gpu::end_frame(ctx.device));
            wz::gpu::present(ctx.device, /*sync_interval*/ 0);
            app.renderer_for_testing().end_frame_capture();
        }

        // The scene's drawable nodes, in authored array order — the order the
        // renderer walks. Everything below re-renders these.
        static std::vector<ea::SceneNodeAsset> drawables(
            const std::vector<ea::SceneNodeAsset>& all)
        {
            std::vector<ea::SceneNodeAsset> out;
            for (const ea::SceneNodeAsset& node : all) {
                if (node.visible && node.renderable_asset
                    && *node.renderable_asset != wz::asset::AssetKey{})
                {
                    out.push_back(node);
                }
            }
            return out;
        }

        // The node indices the renderer submitted, in submission order.
        static std::vector<std::size_t> submission_order(
            const er::RhiSceneRenderer& renderer)
        {
            std::vector<std::size_t> out;
            for (const auto& submitted : renderer.captured_submissions()) {
                out.push_back(submitted.node_index);
            }
            return out;
        }
    };

    // ── The control ───────────────────────────────────────────────────────
    //
    // Before any claim about rungs: does the renderer-side log agree with the
    // command list? A capture that measured only the renderer's intent would
    // be reading with extra steps.
    TEST_F(DrawSubmissionCaptureFixture, RendererSubmissionLogMatchesTheCommandList)
    {
        wz::app::WozzitsApp_v1 app(ctx);
        ASSERT_TRUE(app.load_scene(alpha_order_load_desc()));

        // Realize first: the opening frame builds renderables, and a renderable
        // that failed to realize records nothing.
        render_one_app_frame(app);

        std::vector<CapturedDraw> captured;
        app.renderer_for_testing().begin_frame_capture(&captured);
        render_one_app_frame(app);
        app.renderer_for_testing().end_frame_capture();

        const auto submitted =
            app.renderer_for_testing().captured_submissions();
        ASSERT_FALSE(submitted.empty())
            << "the scene recorded no draws at all";
        for (const auto& entry : submitted) {
            EXPECT_FALSE(entry.rejected)
                << "node " << entry.node_index << " was rejected";
        }

        // One packet handed to record_packet == one DrawInstanced. This is the
        // fact everything below leans on.
        EXPECT_EQ(captured.size(), submitted.size())
            << "the renderer submitted " << submitted.size()
            << " packets but the command list saw " << captured.size()
            << " draws";
    }

    // ── Draw submission (ladder row 7) ────────────────────────────────────
    //
    // The register put the engine at R1+ on "one draw per renderable, no
    // instancing". Both halves measured here.
    TEST_F(DrawSubmissionCaptureFixture, EveryRenderableCostsExactlyOneUninstancedDraw)
    {
        wz::app::WozzitsApp_v1 app(ctx);
        ASSERT_TRUE(app.load_scene(alpha_order_load_desc()));
        render_one_app_frame(app);

        std::vector<CapturedDraw> captured;
        app.renderer_for_testing().begin_frame_capture(&captured);
        render_one_app_frame(app);
        app.renderer_for_testing().end_frame_capture();

        const std::vector<ea::SceneNodeAsset> visible =
            drawables(app.authored_scene_nodes());
        ASSERT_EQ(visible.size(), static_cast<std::size_t>(3))
            << "alpha_order.scene.json should have three drawable nodes";

        // One draw per renderable: three cubes sharing ONE geometry and ONE
        // program — the most batchable shape a scene can have — still cost
        // three draws. That is what R1 means on this ladder, measured rather
        // than read.
        EXPECT_EQ(captured.size(), visible.size())
            << "expected one draw per visible renderable";

        // The capture itself, printed: this test IS the register's evidence
        // for row 7, and a number you cannot read is not evidence.
        std::printf(
            "[ CAPTURE  ] %zu visible renderables -> %zu DrawInstanced calls\n",
            visible.size(),
            captured.size());
        for (std::size_t i = 0; i < captured.size(); ++i) {
            const CapturedDraw& draw = captured[i];
            std::printf(
                "[ CAPTURE  ]   draw %zu: DrawInstanced(vertices=%u, "
                "instances=%u, start=%u)  indexed=%s index_count=%u\n",
                i,
                draw.vertex_count_per_instance,
                draw.instance_count,
                draw.start_vertex_location,
                draw.indexed ? "yes" : "no",
                draw.index_count);
        }

        for (const CapturedDraw& draw : captured) {
            // Nothing in the engine sets DrawArgs::instance_count: rhi's
            // make_draw_args copies index/vertex counts and leaves the field
            // at its default. The name DrawInstanced is D3D12's for a
            // NON-indexed draw and does not imply hardware instancing.
            EXPECT_EQ(draw.instance_count, 1u)
                << "a draw was issued with an instance count other than 1";
            EXPECT_GT(draw.vertex_count_per_instance, 0u);
        }
    }

    // ── Transparency (ladder row 11) ──────────────────────────────────────
    //
    // The claim: submission order does not depend on depth. Measured by
    // rendering the same three transparent cubes from opposite sides of the
    // row, so their back-to-front order is exactly reversed between the two
    // frames. A depth sort anywhere would have to produce different orders.
    TEST_F(DrawSubmissionCaptureFixture, TransparentSubmissionOrderIsTheSameFromBothSides)
    {
        wz::app::WozzitsApp_v1 app(ctx);
        ASSERT_TRUE(app.load_scene(alpha_order_load_desc()));
        render_one_app_frame(app);

        const std::vector<ea::SceneNodeAsset> nodes =
            drawables(app.authored_scene_nodes());
        ASSERT_EQ(nodes.size(), static_cast<std::size_t>(3));

        // The cubes sit at world z = 0, 40, 80, authored in that order.
        // Viewing from z = -60 puts them at view depths 60/100/140 (authored
        // order IS near-to-far); viewing from z = +140 looking back puts them
        // at 140/100/60, the exact reverse.
        const wz::math::Mat4 from_front =
            wz::math::translation(wz::math::Vec3{ 0.0f, 0.0f, 60.0f });
        const wz::math::Mat4 from_behind = wz::math::mul(
            wz::math::scale(wz::math::Vec3{ 1.0f, 1.0f, -1.0f }),
            wz::math::translation(wz::math::Vec3{ 0.0f, 0.0f, -140.0f }));

        render_one_frame_of(
            app, nodes, from_front, wz::math::Vec3{ 0.0f, 0.0f, -60.0f });
        const std::vector<std::size_t> front_order =
            submission_order(app.renderer_for_testing());

        render_one_frame_of(
            app, nodes, from_behind, wz::math::Vec3{ 0.0f, 0.0f, 140.0f });
        const std::vector<std::size_t> behind_order =
            submission_order(app.renderer_for_testing());

        ASSERT_EQ(front_order.size(), static_cast<std::size_t>(3));
        ASSERT_EQ(behind_order.size(), static_cast<std::size_t>(3));

        // Authored order, both times. Transparency is composited in whatever
        // order the scene happens to list its nodes, which is correct only
        // when the author already knew the viewpoint.
        const std::vector<std::size_t> authored{ 0, 1, 2 };
        EXPECT_EQ(front_order, authored);
        EXPECT_EQ(behind_order, authored)
            << "submission order changed with the viewpoint — something IS "
               "depth-sorting, and this test's premise is stale";
    }

    // What DOES decide the order, then. Scene-array position — which is what
    // bake_scene_node_draw_order produces from tree preorder + the authored
    // render_order key. Reversing the array reverses the frame exactly, so the
    // ordering is fully determined upstream of the renderer, and the renderer
    // contributes only its stable DrawLayer partition.
    TEST_F(DrawSubmissionCaptureFixture, SubmissionOrderFollowsTheSceneArrayExactly)
    {
        wz::app::WozzitsApp_v1 app(ctx);
        ASSERT_TRUE(app.load_scene(alpha_order_load_desc()));
        render_one_app_frame(app);

        std::vector<ea::SceneNodeAsset> nodes =
            drawables(app.authored_scene_nodes());
        ASSERT_EQ(nodes.size(), static_cast<std::size_t>(3));

        const wz::math::Mat4 view =
            wz::math::translation(wz::math::Vec3{ 0.0f, 0.0f, 60.0f });
        const wz::math::Vec3 camera{ 0.0f, 0.0f, -60.0f };

        render_one_frame_of(app, nodes, view, camera);
        const std::vector<std::size_t> order =
            submission_order(app.renderer_for_testing());
        EXPECT_EQ(order, (std::vector<std::size_t>{ 0, 1, 2 }));

        std::reverse(nodes.begin(), nodes.end());
        render_one_frame_of(app, nodes, view, camera);
        const std::vector<std::size_t> reversed_order =
            submission_order(app.renderer_for_testing());

        // Still 0,1,2 as INDICES — because the indices are into the span we
        // passed, and we reversed the span. The point is that the renderer
        // walked it in order and did nothing else: had it sorted by anything
        // of its own, the two frames could not both be in span order.
        EXPECT_EQ(reversed_order, (std::vector<std::size_t>{ 0, 1, 2 }));

        // The identities really did swap, which is what makes the above
        // meaningful rather than a tautology.
        EXPECT_EQ(nodes[0].id, drawables(app.authored_scene_nodes())[2].id);
    }

    // And the authored control that reaches that array. render_order is a
    // per-node integer the scene JSON carries, the editor edits, the ABI posts
    // and the fingerprint mixes -- and bake_scene_node_draw_order applies it at
    // load and on every structural edit (wozzits_app_v1.cpp:599,
    // scene_document.cpp:127/156/182/198). So the engine DOES have a
    // transparency-ordering control; it is manual. Worth measuring rather than
    // assuming, because the same chain in cluster A turned out four times to be
    // authored, stored, and read by nothing.
    TEST_F(DrawSubmissionCaptureFixture, AuthoredRenderOrderDecidesWhoDrawsFirst)
    {
        wz::app::WozzitsApp_v1 app(ctx);
        ASSERT_TRUE(app.load_scene(alpha_order_load_desc()));
        render_one_app_frame(app);

        std::vector<ea::SceneNodeAsset> nodes =
            drawables(app.authored_scene_nodes());
        ASSERT_EQ(nodes.size(), static_cast<std::size_t>(3));
        const wz::scene::AuthoredEntityId far_id = nodes[2].id;

        // Author the FAR cube into an earlier layer, then bake as the document
        // does. Lower render_order draws first.
        nodes[0].render_order = 0;
        nodes[1].render_order = 0;
        nodes[2].render_order = -1;
        ea::sort_scene_nodes_by_render_order(nodes);
        EXPECT_EQ(nodes[0].id, far_id)
            << "the bake did not move the far cube to the front of the array";

        render_one_frame_of(
            app,
            nodes,
            wz::math::translation(wz::math::Vec3{ 0.0f, 0.0f, 60.0f }),
            wz::math::Vec3{ 0.0f, 0.0f, -60.0f });

        // Submission follows the baked array, so the far cube now draws first
        // -- which for this viewpoint happens to be the CORRECT back-to-front
        // order. An author can get transparency right here; nothing does it
        // for them, and nothing re-does it when the camera moves.
        EXPECT_EQ(
            submission_order(app.renderer_for_testing()),
            (std::vector<std::size_t>{ 0, 1, 2 }));
    }
}
