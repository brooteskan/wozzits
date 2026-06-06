#include <gtest/gtest.h>
#include <render/ir/render_ir.h>
#include <scene/compile/scene_compiler.h>
#include <scene/compile/legacy_classification.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <numeric>
#include <random>
#include <vector>

using namespace wz::scene;
using namespace wz::render;
using namespace wz::core::graph;
using namespace wz::math;

// ─── Helpers ──────────────────────────────────────────────────────────────────

namespace {

    AABB unit_box()
    {
        return { Vec3{ -0.5f, -0.5f, -0.5f }, Vec3{ 0.5f, 0.5f, 0.5f } };
    }

    ViewData identity_view()
    {
        ViewData v{};
        v.view            = Mat4::identity();
        v.projection      = Mat4::identity();
        v.view_projection = Mat4::identity();
        return v;
    }

    Mat4 translation_z(float z)
    {
        Mat4 m = Mat4::identity();
        m.m[14] = z;
        return m;
    }

    // Build a flat scene: one root + N opaque children.
    struct FlatScene
    {
        SceneStorage                      storage{};
        std::vector<RenderableDescriptor> descs{};
        NodeHandle                        root{};
        std::vector<NodeHandle>           objects{};
    };

    FlatScene make_flat_scene(uint32_t n_objects,
                              MeshHandle mesh    = 1u,
                              MaterialHandle mat = 1u)
    {
        SceneBuilder b;

        TransformNode root_node{};
        root_node.local = Mat4::identity();
        NodeHandle root_h = add_node(b, root_node);

        std::vector<NodeHandle> obj_handles;
        obj_handles.reserve(n_objects);

        for (uint32_t i = 0; i < n_objects; ++i) {
            TransformNode obj_node{};
            obj_node.local = translation_z(static_cast<float>(i));
            obj_node.flags = TransformNodeFlag::RenderDomain;
            NodeHandle obj_h = add_node(b, obj_node);
            EXPECT_TRUE(add_edge(b, root_h, obj_h));
            obj_handles.push_back(obj_h);
        }

        auto result = build(std::move(b));
        EXPECT_TRUE(result.has_value());

        FlatScene out{};
        out.storage = std::move(*result);
        out.root    = root_h;
        out.objects = std::move(obj_handles);

        const uint32_t total = node_count(out.storage.polytree);
        out.descs.resize(total);
        out.descs[root_h] = RenderableDescriptor{
            .node_class = classify_legacy_renderable(RenderPipeline::None),
        };
        for (NodeHandle h : out.objects) {
            out.descs[h] = RenderableDescriptor{
                .node_class   = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
                .mesh         = mesh,
                .material     = mat,
                .local_bounds = unit_box(),
                .visible      = true,
            };
        }

        propagate_all(out.storage.polytree);
        return out;
    }

    // Directly set a node's world matrix, simulating propagation already ran.
    void set_world(SceneGraph& g, NodeHandle n, const Mat4& world)
    {
        const_cast<TransformNode&>(node_data(g, n)).world = world;
    }

} // namespace


// Helper: compiled slot index for an object node.
static uint32_t opaque_slot(const CompiledSceneStorage& st, NodeHandle n)
{
    return st.metadata.node_to_output[n].index;
}


// ─── Empty dirty list treats all nodes as dirty ───────────────────────────────

TEST(DirtyNodeFilter, EmptyDirtyListUpdatesAllNodes)
{
    auto scene = make_flat_scene(3);
    CompiledSceneStorage storage{};
    compile(storage, scene.storage.polytree, scene.descs, {}, identity_view());

    for (uint32_t i = 0; i < scene.objects.size(); ++i)
        set_world(scene.storage.polytree, scene.objects[i], translation_z(100.f + i));

    update_compiled_transforms(storage, scene.storage.polytree, scene.descs,
                               identity_view(), {});

    for (uint32_t i = 0; i < scene.objects.size(); ++i) {
        uint32_t slot = opaque_slot(storage, scene.objects[i]);
        EXPECT_FLOAT_EQ(storage.scene.opaque[slot].world.m[14], 100.f + i)
            << "node " << i << " (slot " << slot << ") not updated with empty dirty list";
    }
}


// ─── Only listed nodes are patched; unlisted nodes are skipped ───────────────

TEST(DirtyNodeFilter, OnlyDirtyNodesArePatched)
{
    auto scene = make_flat_scene(3);
    CompiledSceneStorage storage{};
    compile(storage, scene.storage.polytree, scene.descs, {}, identity_view());

    const uint32_t slot1    = opaque_slot(storage, scene.objects[1]);
    const float    z_before = storage.scene.opaque[slot1].world.m[14];

    set_world(scene.storage.polytree, scene.objects[0], translation_z(10.f));
    set_world(scene.storage.polytree, scene.objects[1], translation_z(20.f)); // moved but NOT in list
    set_world(scene.storage.polytree, scene.objects[2], translation_z(30.f));

    std::vector<NodeHandle> dirty = { scene.objects[0], scene.objects[2] };

    update_compiled_transforms(storage, scene.storage.polytree, scene.descs,
                               identity_view(), dirty);

    EXPECT_FLOAT_EQ(storage.scene.opaque[opaque_slot(storage, scene.objects[0])].world.m[14], 10.f)
        << "dirty node 0 not updated";
    EXPECT_FLOAT_EQ(storage.scene.opaque[slot1].world.m[14], z_before)
        << "unlisted node 1 was updated — dirty filter not applied";
    EXPECT_FLOAT_EQ(storage.scene.opaque[opaque_slot(storage, scene.objects[2])].world.m[14], 30.f)
        << "dirty node 2 not updated";
}

TEST(DirtyNodeFilter, RefreshTransformUsesDirtyNodeList)
{
    auto scene = make_flat_scene(3);
    CompiledSceneStorage storage{};
    compile(storage, scene.storage.polytree, scene.descs, {}, identity_view());

    const uint32_t slot1 = opaque_slot(storage, scene.objects[1]);
    const float z1_before = storage.scene.opaque[slot1].world.m[14];

    set_world(scene.storage.polytree, scene.objects[0], translation_z(100.f));
    set_world(scene.storage.polytree, scene.objects[1], translation_z(200.f));
    set_world(scene.storage.polytree, scene.objects[2], translation_z(300.f));

    std::vector<NodeHandle> dirty = { scene.objects[0], scene.objects[2] };

    refresh_compiled_scene(
        storage,
        scene.storage.polytree,
        scene.descs,
        {},
        identity_view(),
        SceneDirtyBits::Transform,
        dirty);

    EXPECT_FLOAT_EQ(storage.scene.opaque[opaque_slot(storage, scene.objects[0])].world.m[14], 100.f)
        << "dirty node 0 not refreshed";
    EXPECT_FLOAT_EQ(storage.scene.opaque[slot1].world.m[14], z1_before)
        << "refresh_compiled_scene ignored dirty_nodes and patched an unlisted node";
    EXPECT_FLOAT_EQ(storage.scene.opaque[opaque_slot(storage, scene.objects[2])].world.m[14], 300.f)
        << "dirty node 2 not refreshed";
}

TEST(DirtyNodeFilter, FarOffscreenTransformCanBeDeferredAndCaughtUp)
{
    auto scene = make_flat_scene(1);
    CompiledSceneStorage storage{};
    RenderIRStorage ir{};

    set_world(scene.storage.polytree, scene.objects[0], translation_z(5.f));
    compile(storage, scene.storage.polytree, scene.descs, {}, identity_view());
    build_render_ir(ir, storage.scene);
    ASSERT_EQ(ir.ir.opaque.size(), 0u) << "object starts outside the identity frustum";

    set_world(scene.storage.polytree, scene.objects[0], translation_z(10.f));
    std::vector<NodeHandle> no_render_update = { scene.root };
    refresh_compiled_scene(
        storage,
        scene.storage.polytree,
        scene.descs,
        {},
        identity_view(),
        SceneDirtyBits::Transform,
        no_render_update);
    update_render_ir(ir, storage.scene);

    EXPECT_EQ(ir.ir.opaque.size(), 0u)
        << "stale far-offscreen bounds should remain culled while render update is deferred";

    set_world(scene.storage.polytree, scene.objects[0], translation_z(0.f));
    std::vector<NodeHandle> catch_up = { scene.objects[0] };
    refresh_compiled_scene(
        storage,
        scene.storage.polytree,
        scene.descs,
        {},
        identity_view(),
        SceneDirtyBits::Transform,
        catch_up);
    update_render_ir(ir, storage.scene);

    ASSERT_EQ(ir.ir.opaque.size(), 1u)
        << "catch-up update must make the object visible in the first relevant frame";
    EXPECT_FLOAT_EQ(storage.scene.opaque[opaque_slot(storage, scene.objects[0])].world.m[14], 0.f);
}


// ─── Dirty filtering does not reallocate buffers ─────────────────────────────

TEST(DirtyNodeFilter, FilteredUpdateDoesNotReallocate)
{
    auto scene = make_flat_scene(4);
    CompiledSceneStorage storage{};
    compile(storage, scene.storage.polytree, scene.descs, {}, identity_view());

    const std::byte* stable_ptr = storage.stable_buffer.get();
    const std::byte* meta_ptr   = storage.metadata_buffer.get();

    set_world(scene.storage.polytree, scene.objects[0], translation_z(5.f));

    std::vector<NodeHandle> dirty = { scene.objects[0] };

    update_compiled_transforms(storage, scene.storage.polytree, scene.descs,
                               identity_view(), dirty);

    EXPECT_EQ(storage.stable_buffer.get(),   stable_ptr) << "stable_buffer reallocated";
    EXPECT_EQ(storage.metadata_buffer.get(), meta_ptr)   << "metadata_buffer reallocated";
}


// ─── view_changed=false: only dirty nodes get depth updated ──────────────────
//
// With view_changed=false, a clean transparent node's depth is unchanged even
// when its bounds change in the scene graph (it is skipped by the filter),
// while a dirty node's depth is computed inline.

TEST(DirtyNodeFilter, ViewNotChangedSkipsCleanNodeDepth)
{
    SceneBuilder b;
    TransformNode root_node{};
    root_node.local = Mat4::identity();
    NodeHandle root_h = add_node(b, root_node);

    auto make_obj = [&](float z) {
        TransformNode n{};
        n.local = translation_z(z);
        n.flags = TransformNodeFlag::RenderDomain;
        return add_node(b, n);
    };
    NodeHandle obj0 = make_obj(2.f);
    NodeHandle obj1 = make_obj(4.f);
    EXPECT_TRUE(add_edge(b, root_h, obj0));
    EXPECT_TRUE(add_edge(b, root_h, obj1));

    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    SceneStorage scene_storage = std::move(*result);

    const uint32_t total = node_count(scene_storage.polytree);
    std::vector<RenderableDescriptor> descs(total);
    descs[root_h] = { .node_class = classify_legacy_renderable(RenderPipeline::None) };
    descs[obj0]   = {
        .node_class   = classify_legacy_renderable(RenderPipeline::TransparentGeometry),
        .mesh         = 1u,
        .material     = 1u,
        .local_bounds = unit_box(),
        .visible      = true,
    };
    descs[obj1] = descs[obj0];
    propagate_all(scene_storage.polytree);

    CompiledSceneStorage storage{};
    compile(storage, scene_storage.polytree, descs, {}, identity_view());
    ASSERT_EQ(storage.scene.transparent.size(), 2u);

    auto slot = [&](NodeHandle n) {
        return storage.metadata.node_to_output[n].index;
    };

    const float depth0_before = storage.scene.transparent[slot(obj0)].depth;
    const float depth1_before = storage.scene.transparent[slot(obj1)].depth;

    // Move both objects — list only obj0 as dirty.
    set_world(scene_storage.polytree, obj0, translation_z(10.f));
    set_world(scene_storage.polytree, obj1, translation_z(20.f));

    std::vector<NodeHandle> dirty = { obj0 };

    // view_changed=false: depths computed inline only for patched nodes.
    update_compiled_transforms(storage, scene_storage.polytree, descs,
                               identity_view(), dirty, /*view_changed=*/false);

    // obj0 (dirty): depth must reflect new position z=10.
    EXPECT_NE(storage.scene.transparent[slot(obj0)].depth, depth0_before)
        << "dirty transparent node depth not updated with view_changed=false";

    // obj1 (unlisted): depth unchanged — skipped entirely.
    EXPECT_FLOAT_EQ(storage.scene.transparent[slot(obj1)].depth, depth1_before)
        << "unlisted transparent node depth changed — should have been skipped";
}


// ─── DISABLED: benchmark dirty-node list vs. full update ─────────────────────
//
// Run with --gtest_also_run_disabled_tests to measure.
//
// Scene: 100,000 opaque nodes.
// Cases:
//   all dirty (empty list)                     — baseline, O(N)
//   10% dirty, contiguous, view_changed=true
//   10% dirty, contiguous, view_changed=false
//   10% dirty, random,     view_changed=true
//   10% dirty, random,     view_changed=false
//    1% dirty, contiguous, view_changed=true
//    1% dirty, contiguous, view_changed=false
//    1% dirty, random,     view_changed=true
//    1% dirty, random,     view_changed=false
//
// Random distribution is more realistic than contiguous — it exercises
// pointer chasing through node_to_output for non-adjacent node handles.

TEST(DirtyNodeFilter, BenchmarkDirtyNodeDispatch)
{
    constexpr uint32_t N = 100'000;
    constexpr uint32_t M = 50;

    auto scene = make_flat_scene(N);
    CompiledSceneStorage storage{};
    compile(storage, scene.storage.polytree, scene.descs, {}, identity_view());

    for (uint32_t i = 0; i < N; ++i)
        set_world(scene.storage.polytree, scene.objects[i],
                  translation_z(static_cast<float>(i) * 0.1f));

    auto bench = [&](const char* label,
                     std::span<const NodeHandle> dirty,
                     bool vc = true) {
        update_compiled_transforms(storage, scene.storage.polytree, scene.descs,
                                   identity_view(), dirty, vc);
        auto t0 = std::chrono::high_resolution_clock::now();
        for (uint32_t iter = 0; iter < M; ++iter)
            update_compiled_transforms(storage, scene.storage.polytree, scene.descs,
                                       identity_view(), dirty, vc);
        auto t1 = std::chrono::high_resolution_clock::now();
        double avg_us = std::chrono::duration<double, std::micro>(t1 - t0).count() / M;
        std::printf("  %-52s  %8.1f us/call\n", label, avg_us);
    };

    std::printf("\nupdate_compiled_transforms benchmark  (%u nodes, %u iterations)\n", N, M);
    std::printf("  %-52s  %8s\n", "Case", "avg time");
    std::printf("  %s\n", std::string(66, '-').c_str());

    // Baseline — all dirty (empty span).
    bench("all dirty (empty list), vc=true", {}, true);

    // Build dirty lists.
    // Contiguous: every Kth object in scene order.
    // Random: shuffled using a fixed seed for reproducibility.
    std::vector<NodeHandle> contiguous_10pct, contiguous_1pct;
    std::vector<NodeHandle> random_10pct, random_1pct;

    contiguous_10pct.reserve(N / 10);
    contiguous_1pct.reserve(N / 100);
    for (uint32_t i = 0; i < N; i += 10)  contiguous_10pct.push_back(scene.objects[i]);
    for (uint32_t i = 0; i < N; i += 100) contiguous_1pct.push_back(scene.objects[i]);

    // Random selection: copy objects, shuffle, take first K%.
    std::vector<NodeHandle> shuffled = scene.objects;
    std::mt19937 rng(0xBEEF1234u);
    std::shuffle(shuffled.begin(), shuffled.end(), rng);
    random_10pct.assign(shuffled.begin(), shuffled.begin() + N / 10);
    random_1pct.assign(shuffled.begin(),  shuffled.begin() + N / 100);

    bench("10% dirty, contiguous, vc=true",  contiguous_10pct, true);
    bench("10% dirty, contiguous, vc=false", contiguous_10pct, false);
    bench("10% dirty, random,     vc=true",  random_10pct,     true);
    bench("10% dirty, random,     vc=false", random_10pct,     false);

    bench(" 1% dirty, contiguous, vc=true",  contiguous_1pct, true);
    bench(" 1% dirty, contiguous, vc=false", contiguous_1pct, false);
    bench(" 1% dirty, random,     vc=true",  random_1pct,     true);
    bench(" 1% dirty, random,     vc=false", random_1pct,     false);

    std::printf("\n");
    SUCCEED();
}
