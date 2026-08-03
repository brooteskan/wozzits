// tests/jobs/engine_adversarial_tests.cpp
//
// Adversarial tests for the window-engine job graph, frame execution, and
// scene instantiation layers — hardening these systems before building
// collision/physics frame products on top of them.
//
// Test groups:
//   EngineJobs   — job graph template patterns collision will need
//   EngineFrame  — FrameExecution binding and multi-frame reset cycles
//   EngineScene  — scene instantiation edge cases relevant to collision

#include <gtest/gtest.h>

#include <jobs/job_graph_template.h>
#include <jobs/frame_execution.h>
#include <jobs/dag_scheduler.h>
#include <jobs/job_profiler.h>

#include <engine/assets/scene/scene_instance.h>
#include <engine/assets/scene/scene_asset_data.h>

#include <scene/scene_graph.h>
#include <scene/compile/scene_compiler.h>
#include <render/ir/render_ir.h>
#include <render/frame/render_frame.h>

#include <math/mat4.h>
#include <math/projection.h>

#include <algorithm>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

using namespace wz::jobs;
using namespace wz::scene;
using namespace wz::core::graph;
using namespace wz::math;

// ─── Helpers ─────────────────────────────────────────────────────────────────

namespace {

    JobFn record_fn = [](JobContext& ctx)
    {
        static_cast<std::vector<NodeHandle>*>(ctx.frame_user)->push_back(ctx.node);
    };

    void bind_all(FrameExecution& exec, const JobGraphTemplate& tmpl,
                  std::vector<NodeHandle>& out)
    {
        for (uint32_t i = 0; i < tmpl.node_count(); ++i)
            exec.bind(static_cast<NodeHandle>(i), &out);
    }

    // Build a minimal scene for instantiation tests.
    wz::engine::assets::SceneAssetData make_asset_scene(
        int node_count,
        bool add_terrain = false,
        bool add_ground_boundary = false)
    {
        using namespace wz::engine::assets;

        SceneAssetData scene{};
        scene.name = "test_scene";

        // Root node
        SceneNodeAsset root{};
        root.id = "root";
        root.name = "root";
        scene.nodes.push_back(root);

        for (int i = 0; i < node_count; ++i) {
            SceneNodeAsset node{};
            node.id = "node_" + std::to_string(i);
            node.name = "Node " + std::to_string(i);
            node.parent_id = "root";

            AuthoredTransform t{};
            t.translation[0] = static_cast<float>(i);
            node.local = t;

            if (add_terrain && i == 0) {
                node.terrain = SceneTerrainAsset{};
            }
            if (add_ground_boundary && i == 0) {
                SceneGroundBoundaryAsset gb{};
                gb.min[0] = -10.f; gb.min[1] = -10.f; gb.min[2] = -10.f;
                gb.max[0] =  10.f; gb.max[1] =  10.f; gb.max[2] =  10.f;
                node.ground_boundary = gb;
            }

            scene.nodes.push_back(node);
        }

        return scene;
    }

} // namespace


// ═══════════════════════════════════════════════════════════════════════════════
// EngineJobs — Job graph patterns for collision frame insertion
// ═══════════════════════════════════════════════════════════════════════════════


// ─────────────────────────────────────────────────────────────────────────────
// 1. Linear chain with 8 nodes (mirrors the real app pipeline)
// ─────────────────────────────────────────────────────────────────────────────
// The real pipeline is:
//   platform_events → shutdown_input → camera_update → build_view
//   → compile_scene → build_collision_frame
//   → build_render_ir → build_render_frame
//
// Collision runs between compile_scene and build_render_ir.
// This test validates an 8-node linear chain executes in strict order.

TEST(EngineJobs, EightNodeLinearChainExecutesInOrder)
{
    JobGraphTemplate tmpl;

    NodeHandle nodes[8];
    for (int i = 0; i < 8; ++i)
        nodes[i] = tmpl.add_job({ .name = "node", .run = record_fn });

    for (int i = 0; i < 7; ++i)
        ASSERT_TRUE(tmpl.add_dependency(nodes[i], nodes[i + 1]));

    ASSERT_TRUE(tmpl.commit());
    EXPECT_EQ(tmpl.node_count(), 8u);

    FrameExecution exec;
    exec.reset(tmpl);

    std::vector<NodeHandle> order;
    bind_all(exec, tmpl, order);

    DagScheduler sched;
    sched.execute(tmpl, exec);

    ASSERT_EQ(order.size(), 8u);
    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(order[i], nodes[i]) << "node " << i << " out of order";
}


// ─────────────────────────────────────────────────────────────────────────────
// 2. Inserting a node into a linear chain (collision insertion pattern)
// ─────────────────────────────────────────────────────────────────────────────
// Build: A → B → D.  Then insert C between B and D: A → B → C → D.
// C must execute after B and before D.

TEST(EngineJobs, InsertNodeIntoLinearChain)
{
    JobGraphTemplate tmpl;

    auto a = tmpl.add_job({ .name = "compile_scene",     .run = record_fn });
    auto b = tmpl.add_job({ .name = "build_collision_frame", .run = record_fn });
    auto c = tmpl.add_job({ .name = "build_render_ir",   .run = record_fn });
    auto d = tmpl.add_job({ .name = "build_render_frame",.run = record_fn });

    // Chain: A → B → C → D
    ASSERT_TRUE(tmpl.add_dependency(a, b));
    ASSERT_TRUE(tmpl.add_dependency(b, c));
    ASSERT_TRUE(tmpl.add_dependency(c, d));

    ASSERT_TRUE(tmpl.commit());

    FrameExecution exec;
    exec.reset(tmpl);

    std::vector<NodeHandle> order;
    bind_all(exec, tmpl, order);

    DagScheduler sched;
    sched.execute(tmpl, exec);

    ASSERT_EQ(order.size(), 4u);
    EXPECT_EQ(order[0], a);
    EXPECT_EQ(order[1], b);
    EXPECT_EQ(order[2], c);
    EXPECT_EQ(order[3], d);
}


// ─────────────────────────────────────────────────────────────────────────────
// 3. Diamond dependency — collision and render IR in parallel
// ─────────────────────────────────────────────────────────────────────────────
// Future optimization: collision and render IR can run in parallel after
// compile_scene, with render_frame depending on both.
//
//   compile_scene → build_collision_frame ─┐
//                 → build_render_ir ────────┴→ build_render_frame
//
// Both collision and render_ir must run before render_frame.

TEST(EngineJobs, DiamondDependencyCollisionParallelWithRenderIR)
{
    JobGraphTemplate tmpl;

    auto compile     = tmpl.add_job({ .name = "compile_scene",       .run = record_fn });
    auto collision   = tmpl.add_job({ .name = "build_collision_frame", .run = record_fn });
    auto render_ir   = tmpl.add_job({ .name = "build_render_ir",     .run = record_fn });
    auto render_frame= tmpl.add_job({ .name = "build_render_frame",  .run = record_fn });

    ASSERT_TRUE(tmpl.add_dependency(compile, collision));
    ASSERT_TRUE(tmpl.add_dependency(compile, render_ir));
    ASSERT_TRUE(tmpl.add_dependency(collision, render_frame));
    ASSERT_TRUE(tmpl.add_dependency(render_ir, render_frame));

    ASSERT_TRUE(tmpl.commit());

    // Verify indegrees.
    auto deg = tmpl.indegree();
    EXPECT_EQ(deg[compile], 0u);
    EXPECT_EQ(deg[collision], 1u);
    EXPECT_EQ(deg[render_ir], 1u);
    EXPECT_EQ(deg[render_frame], 2u);

    FrameExecution exec;
    exec.reset(tmpl);

    std::vector<NodeHandle> order;
    bind_all(exec, tmpl, order);

    DagScheduler sched;
    sched.execute(tmpl, exec);

    ASSERT_EQ(order.size(), 4u);

    // compile must be first.
    EXPECT_EQ(order[0], compile);

    // render_frame must be last.
    EXPECT_EQ(order[3], render_frame);

    // collision and render_ir are in positions 1 and 2 (either order).
    std::vector<NodeHandle> middle(order.begin() + 1, order.begin() + 3);
    EXPECT_TRUE(std::find(middle.begin(), middle.end(), collision) != middle.end());
    EXPECT_TRUE(std::find(middle.begin(), middle.end(), render_ir) != middle.end());
}


// ─────────────────────────────────────────────────────────────────────────────
// 4. Per-node binding — different frame_user pointers per job
// ─────────────────────────────────────────────────────────────────────────────
// The collision job should receive a different binding than the render jobs.
// Verify that per-node bind() delivers the correct pointer to each job.

// Job that stores frame_user into the void** provided via node_user.
void capture_frame_user(JobContext& ctx)
{
    *static_cast<void**>(ctx.node_user) = ctx.frame_user;
}

TEST(EngineJobs, PerNodeBindingDeliversCorrectPointers)
{
    int data_a = 1;
    int data_b = 2;
    int data_c = 3;

    void* seen_a = nullptr;
    void* seen_b = nullptr;
    void* seen_c = nullptr;

    JobGraphTemplate tmpl;
    auto a = tmpl.add_job({ .name = "a", .run = capture_frame_user, .user = &seen_a });
    auto b = tmpl.add_job({ .name = "b", .run = capture_frame_user, .user = &seen_b });
    auto c = tmpl.add_job({ .name = "c", .run = capture_frame_user, .user = &seen_c });
    tmpl.add_dependency(a, b);
    tmpl.add_dependency(b, c);
    ASSERT_TRUE(tmpl.commit());

    FrameExecution exec;
    exec.reset(tmpl);

    exec.bind(a, &data_a);
    exec.bind(b, &data_b);
    exec.bind(c, &data_c);

    DagScheduler sched;
    sched.execute(tmpl, exec);

    EXPECT_EQ(seen_a, &data_a) << "job A should receive data_a";
    EXPECT_EQ(seen_b, &data_b) << "job B should receive data_b";
    EXPECT_EQ(seen_c, &data_c) << "job C should receive data_c";
}


// ─────────────────────────────────────────────────────────────────────────────
// 5. Multi-frame reset cycle — 20 frames with same graph
// ─────────────────────────────────────────────────────────────────────────────
// Simulates the game loop: reset → bind → execute × 20 frames.
// Every frame must produce identical execution order and Done status.

TEST(EngineFrame, TwentyFrameResetCycle)
{
    JobGraphTemplate tmpl;

    auto a = tmpl.add_job({ .name = "a", .run = record_fn });
    auto b = tmpl.add_job({ .name = "b", .run = record_fn });
    auto c = tmpl.add_job({ .name = "c", .run = record_fn });
    tmpl.add_dependency(a, b);
    tmpl.add_dependency(b, c);
    ASSERT_TRUE(tmpl.commit());

    DagScheduler sched;

    for (int frame = 0; frame < 20; ++frame) {
        FrameExecution exec;
        exec.reset(tmpl);

        std::vector<NodeHandle> order;
        bind_all(exec, tmpl, order);

        sched.execute(tmpl, exec);

        ASSERT_EQ(order.size(), 3u) << "frame " << frame;
        EXPECT_EQ(order[0], a) << "frame " << frame;
        EXPECT_EQ(order[1], b) << "frame " << frame;
        EXPECT_EQ(order[2], c) << "frame " << frame;

        EXPECT_EQ(exec.status[a], JobStatus::Done) << "frame " << frame;
        EXPECT_EQ(exec.status[b], JobStatus::Done) << "frame " << frame;
        EXPECT_EQ(exec.status[c], JobStatus::Done) << "frame " << frame;
        EXPECT_EQ(exec.remaining_jobs, 0u) << "frame " << frame;
    }
}


// ─────────────────────────────────────────────────────────────────────────────
// 6. Profiler records timing for all jobs
// ─────────────────────────────────────────────────────────────────────────────
// Verify that FrameJobProfile captures a timing record for every job node.

TEST(EngineFrame, ProfilerCapturesAllJobs)
{
    JobGraphTemplate tmpl;

    auto a = tmpl.add_job({ .name = "view",    .run = record_fn });
    auto b = tmpl.add_job({ .name = "compile", .run = record_fn });
    auto c = tmpl.add_job({ .name = "ir",      .run = record_fn });
    tmpl.add_dependency(a, b);
    tmpl.add_dependency(b, c);
    ASSERT_TRUE(tmpl.commit());

    FrameExecution exec;
    exec.reset(tmpl);

    std::vector<NodeHandle> order;
    bind_all(exec, tmpl, order);

    FrameJobProfile profile;
    profile.reset(0);

    DagScheduler sched;
    sched.set_profile(&profile);
    sched.execute(tmpl, exec);
    sched.set_profile(nullptr);

    ASSERT_EQ(profile.timings.size(), 3u);

    // Verify names are preserved.
    std::vector<std::string> names;
    for (const auto& t : profile.timings)
        names.push_back(t.name ? t.name : "<null>");

    EXPECT_TRUE(std::find(names.begin(), names.end(), "view") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "compile") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "ir") != names.end());

    // Each timing should have non-zero or at least non-inverted start/end.
    for (const auto& t : profile.timings) {
        EXPECT_LE(t.start_ticks, t.end_ticks)
            << "timing for " << (t.name ? t.name : "<null>")
            << " has inverted start/end";
    }
}


// ─────────────────────────────────────────────────────────────────────────────
// 7. Large fan-out graph — one root fans into 20 leaves
// ─────────────────────────────────────────────────────────────────────────────

TEST(EngineJobs, FanOutTwentyLeaves)
{
    JobGraphTemplate tmpl;

    auto root = tmpl.add_job({ .name = "root", .run = record_fn });

    constexpr int N = 20;
    NodeHandle leaves[N];
    for (int i = 0; i < N; ++i) {
        leaves[i] = tmpl.add_job({ .name = "leaf", .run = record_fn });
        ASSERT_TRUE(tmpl.add_dependency(root, leaves[i]));
    }

    ASSERT_TRUE(tmpl.commit());

    FrameExecution exec;
    exec.reset(tmpl);

    std::vector<NodeHandle> order;
    bind_all(exec, tmpl, order);

    DagScheduler sched;
    sched.execute(tmpl, exec);

    ASSERT_EQ(order.size(), static_cast<size_t>(N + 1));
    EXPECT_EQ(order[0], root) << "root must execute first";

    // All leaves must appear in order[1..N].
    for (int i = 0; i < N; ++i) {
        EXPECT_TRUE(std::find(order.begin() + 1, order.end(), leaves[i]) != order.end())
            << "leaf " << i << " was not executed";
    }

    EXPECT_EQ(exec.remaining_jobs, 0u);
}


// ─────────────────────────────────────────────────────────────────────────────
// 8. Wide fan-in — 10 roots fan into one join node
// ─────────────────────────────────────────────────────────────────────────────

TEST(EngineJobs, FanInTenRoots)
{
    JobGraphTemplate tmpl;

    constexpr int N = 10;
    NodeHandle roots[N];
    for (int i = 0; i < N; ++i)
        roots[i] = tmpl.add_job({ .name = "root", .run = record_fn });

    auto join = tmpl.add_job({ .name = "join", .run = record_fn });
    for (int i = 0; i < N; ++i)
        ASSERT_TRUE(tmpl.add_dependency(roots[i], join));

    ASSERT_TRUE(tmpl.commit());

    auto deg = tmpl.indegree();
    EXPECT_EQ(deg[join], static_cast<uint32_t>(N));

    FrameExecution exec;
    exec.reset(tmpl);

    std::vector<NodeHandle> order;
    bind_all(exec, tmpl, order);

    DagScheduler sched;
    sched.execute(tmpl, exec);

    ASSERT_EQ(order.size(), static_cast<size_t>(N + 1));
    EXPECT_EQ(order.back(), join) << "join must execute last";
    EXPECT_EQ(exec.remaining_jobs, 0u);
}


// ═══════════════════════════════════════════════════════════════════════════════
// EngineScene — Scene instantiation edge cases
// ═══════════════════════════════════════════════════════════════════════════════


// ─────────────────────────────────────────────────────────────────────────────
// 9. Empty scene instantiation — root-only asset
// ─────────────────────────────────────────────────────────────────────────────

TEST(EngineScene, EmptySceneInstantiates)
{
    auto scene = make_asset_scene(0);

    auto result = wz::engine::assets::instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    EXPECT_EQ(node_count(result.instance.storage.polytree), 1u)
        << "root-only scene should have 1 node";
    EXPECT_EQ(result.instance.renderables.size(), 1u);
    EXPECT_TRUE(result.instance.input_receivers.empty());
    EXPECT_TRUE(result.instance.terrains.empty());
    EXPECT_TRUE(result.instance.ground_boundaries.empty());
}


// ─────────────────────────────────────────────────────────────────────────────
// 10. Scene with many children — 50 nodes under root
// ─────────────────────────────────────────────────────────────────────────────

TEST(EngineScene, FiftyNodeSceneInstantiates)
{
    constexpr int N = 50;
    auto scene = make_asset_scene(N);

    auto result = wz::engine::assets::instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    // root + N children
    EXPECT_EQ(node_count(result.instance.storage.polytree),
              static_cast<uint32_t>(N + 1));

    // Authored→runtime and runtime→authored maps should be consistent.
    EXPECT_EQ(result.instance.runtime_to_authored.size(),
              static_cast<size_t>(N + 1));
    EXPECT_EQ(result.instance.authored_to_runtime.size(),
              static_cast<size_t>(N + 1));

    // Verify every authored id resolves to a valid runtime handle.
    for (const auto& node : scene.nodes) {
        auto it = result.instance.authored_to_runtime.find(node.id);
        EXPECT_NE(it, result.instance.authored_to_runtime.end())
            << "authored id '" << node.id << "' not found in runtime map";
    }
}


// ─────────────────────────────────────────────────────────────────────────────
// 11. Scene with terrain component — component table populated
// ─────────────────────────────────────────────────────────────────────────────

TEST(EngineScene, TerrainComponentInstantiates)
{
    auto scene = make_asset_scene(3, /*add_terrain=*/true);

    auto result = wz::engine::assets::instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    EXPECT_EQ(result.instance.terrains.size(), 1u)
        << "scene with one terrain component should have 1 terrain record";

    // The terrain should be on the first child node.
    auto it = result.instance.authored_to_runtime.find("node_0");
    ASSERT_NE(it, result.instance.authored_to_runtime.end());
    EXPECT_EQ(result.instance.terrains[0].node, it->second);
}


// ─────────────────────────────────────────────────────────────────────────────
// 12. Scene with ground boundary — component table populated
// ─────────────────────────────────────────────────────────────────────────────

TEST(EngineScene, GroundBoundaryComponentInstantiates)
{
    auto scene = make_asset_scene(2, /*add_terrain=*/false,
                                     /*add_ground_boundary=*/true);

    auto result = wz::engine::assets::instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    EXPECT_EQ(result.instance.ground_boundaries.size(), 1u);

    const auto& gb = result.instance.ground_boundaries[0].component;
    EXPECT_FLOAT_EQ(gb.min[0], -10.f);
    EXPECT_FLOAT_EQ(gb.max[0],  10.f);
}


// ─────────────────────────────────────────────────────────────────────────────
// 13. Duplicate node id is rejected
// ─────────────────────────────────────────────────────────────────────────────

TEST(EngineScene, DuplicateNodeIdRejected)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "dup_test";

    SceneNodeAsset a{}; a.id = "same_id"; a.name = "A";
    SceneNodeAsset b{}; b.id = "same_id"; b.name = "B";

    scene.nodes.push_back(a);
    scene.nodes.push_back(b);

    auto result = instantiate_scene(scene);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, SceneInstantiateError::DuplicateNodeId);
}


// ─────────────────────────────────────────────────────────────────────────────
// 14. Missing parent id is rejected
// ─────────────────────────────────────────────────────────────────────────────

TEST(EngineScene, MissingParentIdRejected)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "missing_parent";

    SceneNodeAsset root{}; root.id = "root"; root.name = "root";
    SceneNodeAsset child{};
    child.id = "child";
    child.name = "child";
    child.parent_id = "nonexistent";

    scene.nodes.push_back(root);
    scene.nodes.push_back(child);

    auto result = instantiate_scene(scene);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, SceneInstantiateError::ParentNotFound);
}


// ─────────────────────────────────────────────────────────────────────────────
// 15. Transform propagation after instantiation
// ─────────────────────────────────────────────────────────────────────────────
// Verify that instantiate_scene propagates world transforms correctly.
// A child at local translation (5, 0, 0) under root at origin should
// have world position (5, 0, 0).

TEST(EngineScene, TransformPropagatedAfterInstantiation)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "transform_test";

    SceneNodeAsset root{};
    root.id = "root";
    root.name = "root";
    scene.nodes.push_back(root);

    SceneNodeAsset child{};
    child.id = "child";
    child.name = "child";
    child.parent_id = "root";
    child.local.translation[0] = 5.0f;
    scene.nodes.push_back(child);

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    auto child_handle = result.instance.authored_to_runtime.at("child");
    const auto& world = node_data(
        result.instance.storage.polytree, child_handle).world;

    EXPECT_FLOAT_EQ(world.m[12], 5.0f) << "child world X should be 5";
    EXPECT_FLOAT_EQ(world.m[13], 0.0f);
    EXPECT_FLOAT_EQ(world.m[14], 0.0f);
}


// ─────────────────────────────────────────────────────────────────────────────
// 16. Multiple component types on same node
// ─────────────────────────────────────────────────────────────────────────────
// A single node can have both a terrain component and a ground boundary.
// Both component tables should contain a record for the same node handle.

TEST(EngineScene, MultipleComponentsOnSameNode)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "multi_component";

    SceneNodeAsset root{};
    root.id = "root";
    root.name = "root";
    scene.nodes.push_back(root);

    SceneNodeAsset node{};
    node.id = "combined";
    node.name = "combined";
    node.parent_id = "root";
    node.terrain = SceneTerrainAsset{};
    SceneGroundBoundaryAsset gb{};
    gb.min[0] = -1.f; gb.max[0] = 1.f;
    node.ground_boundary = gb;
    SceneEventListenerAsset el{};
    el.channels.push_back("collision");
    node.event_listener = el;
    scene.nodes.push_back(node);

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    auto handle = result.instance.authored_to_runtime.at("combined");

    ASSERT_EQ(result.instance.terrains.size(), 1u);
    EXPECT_EQ(result.instance.terrains[0].node, handle);

    ASSERT_EQ(result.instance.ground_boundaries.size(), 1u);
    EXPECT_EQ(result.instance.ground_boundaries[0].node, handle);

    ASSERT_EQ(result.instance.event_listeners.size(), 1u);
    EXPECT_EQ(result.instance.event_listeners[0].node, handle);
    EXPECT_EQ(result.instance.event_listeners[0].component.channels.size(), 1u);
    EXPECT_EQ(result.instance.event_listeners[0].component.channels[0], "collision");
}


// ─────────────────────────────────────────────────────────────────────────────
// 17. Component summary counts match component tables
// ─────────────────────────────────────────────────────────────────────────────

TEST(EngineScene, ComponentSummaryMatchesTables)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "summary_test";

    SceneNodeAsset root{};
    root.id = "root";
    root.name = "root";
    scene.nodes.push_back(root);

    // Add several components across nodes.
    for (int i = 0; i < 3; ++i) {
        SceneNodeAsset n{};
        n.id = "n" + std::to_string(i);
        n.name = n.id;
        n.parent_id = "root";

        if (i == 0) n.terrain = SceneTerrainAsset{};
        if (i == 1) {
            SceneGroundBoundaryAsset gb{};
            n.ground_boundary = gb;
        }
        if (i == 2) n.audio_listener = SceneAudioListenerAsset{};

        scene.nodes.push_back(n);
    }

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok());

    auto summary = summarize_scene_instance_components(result.instance);

    EXPECT_EQ(summary.terrains,
              static_cast<uint32_t>(result.instance.terrains.size()));
    EXPECT_EQ(summary.ground_boundaries,
              static_cast<uint32_t>(result.instance.ground_boundaries.size()));
    EXPECT_EQ(summary.audio_listeners,
              static_cast<uint32_t>(result.instance.audio_listeners.size()));
    EXPECT_EQ(summary.runtime_entities,
              node_count(result.instance.storage.polytree));
}


// ─────────────────────────────────────────────────────────────────────────────
// 18. Deep authored hierarchy — 3-level parent chain
// ─────────────────────────────────────────────────────────────────────────────
// root → parent → child → grandchild
// Each level translates +1 on X.  Grandchild world X should be 3.

TEST(EngineScene, DeepAuthoredHierarchyTransforms)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "deep_hierarchy";

    auto make_node = [](const std::string& id,
                        const std::string& parent,
                        float tx) -> SceneNodeAsset
    {
        SceneNodeAsset n{};
        n.id = id;
        n.name = id;
        if (!parent.empty()) n.parent_id = parent;
        n.local.translation[0] = tx;
        return n;
    };

    scene.nodes.push_back(make_node("root", "", 0.f));
    scene.nodes.push_back(make_node("parent", "root", 1.f));
    scene.nodes.push_back(make_node("child", "parent", 1.f));
    scene.nodes.push_back(make_node("grandchild", "child", 1.f));

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    auto gc_handle = result.instance.authored_to_runtime.at("grandchild");
    const auto& gc_world = node_data(
        result.instance.storage.polytree, gc_handle).world;

    EXPECT_FLOAT_EQ(gc_world.m[12], 3.0f)
        << "grandchild world X should be 1+1+1 = 3";
}
