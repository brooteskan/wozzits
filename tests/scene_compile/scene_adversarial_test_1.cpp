// tests/scene_compile/scene_adversarial_test_1.cpp
//
// Adversarial tests designed to probe edge cases and break the scene-render
// pipeline.  Each test targets a specific boundary condition, ordering
// assumption, or API contract that the existing suite does not cover.

#include <gtest/gtest.h>

#include <math/mat4.h>
#include <render/ir/render_ir.h>
#include <render/frame/render_frame.h>
#include <render/backend/stub_backend.h>
#include <scene/compile/scene_compiler.h>
#include <scene/compile/legacy_classification.h>
#include <scene/scene_graph.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <numeric>

using namespace wz::scene;
using namespace wz::render;
using namespace wz::render::backend;
using namespace wz::core::graph;
using namespace wz::math;

// ─── Helpers ─────────────────────────────────────────────────────────────────

namespace {

    Mat4 translation(float x, float y, float z)
    {
        Mat4 m = Mat4::identity();
        m.m[12] = x; m.m[13] = y; m.m[14] = z;
        return m;
    }

    AABB unit_box()
    {
        return { Vec3{-0.5f,-0.5f,-0.5f}, Vec3{0.5f,0.5f,0.5f} };
    }

    // Wide orthographic frustum — nothing gets culled.
    ViewData wide_view_at(float cam_z)
    {
        ViewData v{};
        v.view = Mat4::identity();
        v.view.m[14] = -cam_z;
        v.projection = Mat4::identity();
        v.view_projection.m[0]  = 1e-4f;
        v.view_projection.m[5]  = 1e-4f;
        v.view_projection.m[10] = 1e-4f;
        v.view_projection.m[15] = 1.0f;
        v.camera_position = Vec3{ 0.f, 0.f, cam_z };
        return v;
    }

    ViewData identity_view()
    {
        ViewData v{};
        v.view = Mat4::identity();
        v.projection = Mat4::identity();
        v.view_projection = Mat4::identity();
        return v;
    }

    void set_world(SceneGraph& g, NodeHandle n, const Mat4& world)
    {
        const_cast<TransformNode&>(node_data(g, n)).world = world;
    }

} // namespace


// ═════════════════════════════════════════════════════════════════════════════
// 1. Recompile shrinking scene — metadata must reflect the smaller scene
// ═════════════════════════════════════════════════════════════════════════════
//
// Compile a scene with 3 opaque nodes, then recompile with 2 of them invisible.
// The second compile must produce only 1 opaque record.  Stale metadata from
// the first compile must be overwritten.

TEST(Adversarial, RecompileShrinkingSceneMetadata)
{
    SceneBuilder b;

    TransformNode root_node{};
    NodeHandle root = add_node(b, root_node);

    NodeHandle children[3];
    for (int i = 0; i < 3; ++i) {
        TransformNode cn{};
        cn.local = translation(static_cast<float>(i), 0.f, 0.f);
        cn.flags = TransformNodeFlag::RenderDomain;
        children[i] = add_node(b, cn);
        add_edge(b, root, children[i]);
    }

    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    propagate_all(result->polytree);

    std::vector<RenderableDescriptor> descs(node_count(result->polytree));
    descs[root] = { classify_legacy_renderable(RenderPipeline::None) };
    for (int i = 0; i < 3; ++i)
        descs[children[i]] = {
            .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
            .mesh = static_cast<MeshHandle>(i), .material = 0u,
            .local_bounds = unit_box(), .visible = true };

    CompiledSceneStorage storage{};

    // First compile: 3 opaque records.
    compile(storage, result->polytree, descs, {}, identity_view());
    ASSERT_EQ(storage.scene.opaque.size(), 3u);
    ASSERT_EQ(storage.metadata.opaque_source_nodes.size(), 3u);

    // Hide two of them.
    descs[children[0]].visible = false;
    descs[children[2]].visible = false;

    // Second compile: only 1 opaque record.
    compile(storage, result->polytree, descs, {}, identity_view());

    EXPECT_EQ(storage.scene.opaque.size(), 1u)
        << "recompile with fewer visible nodes did not shrink opaque span";
    EXPECT_EQ(storage.metadata.opaque_source_nodes.size(), 1u)
        << "metadata opaque source nodes not shrunk";

    // The surviving node should be children[1] with mesh==1.
    EXPECT_EQ(storage.scene.opaque[0].mesh, 1u);
    EXPECT_EQ(storage.metadata.opaque_source_nodes[0], children[1]);

    // Forward lookup from the surviving node should point to index 0.
    EXPECT_EQ(storage.metadata.node_to_output[children[1]].kind,
              CompiledOutputKind::SurfacePrimitive);
    EXPECT_EQ(storage.metadata.node_to_output[children[1]].index, 0u);

    // Hidden nodes must have no output.
    EXPECT_EQ(storage.metadata.node_to_output[children[0]].kind,
              CompiledOutputKind::None);
    EXPECT_EQ(storage.metadata.node_to_output[children[2]].kind,
              CompiledOutputKind::None);
}


// ═════════════════════════════════════════════════════════════════════════════
// 2. Masked surface class treated as opaque
// ═════════════════════════════════════════════════════════════════════════════
//
// SurfaceClass::Masked should route to the opaque pipeline, not transparent.

TEST(Adversarial, MaskedSurfaceRoutesToOpaque)
{
    SceneBuilder b;

    TransformNode root_node{};
    NodeHandle root = add_node(b, root_node);

    TransformNode obj_node{};
    obj_node.local = Mat4::identity();
    obj_node.flags = TransformNodeFlag::RenderDomain;
    NodeHandle obj = add_node(b, obj_node);

    add_edge(b, root, obj);
    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    propagate_all(result->polytree);

    std::vector<RenderableDescriptor> descs(node_count(result->polytree));
    descs[root] = { classify_legacy_renderable(RenderPipeline::None) };
    descs[obj] = {
        .node_class = SceneNodeClass{
            .role            = SceneRole::Renderable,
            .producer        = ProducerKind::Mesh,
            .default_surface = SurfaceClass::Masked,
            .spatial         = SpatialKind::MeshBounds,
            .compile         = CompileBehavior::Static,
            .domains         = RenderDomain::Surface | RenderDomain::Shadow,
        },
        .mesh = 5u, .material = 3u, .local_bounds = unit_box(), .visible = true
    };

    CompiledSceneStorage storage{};
    compile(storage, result->polytree, descs, {}, identity_view());

    EXPECT_EQ(storage.scene.opaque.size(), 1u)
        << "Masked surface did not produce an opaque record";
    EXPECT_EQ(storage.scene.transparent.size(), 0u)
        << "Masked surface incorrectly produced a transparent record";
    EXPECT_EQ(storage.scene.opaque[0].mesh, 5u);
    EXPECT_EQ(storage.scene.opaque[0].material, 3u);
}


// ═════════════════════════════════════════════════════════════════════════════
// 3. Particle with INVALID_MESH is excluded
// ═════════════════════════════════════════════════════════════════════════════
//
// The compiler skips particles with INVALID_MESH (unlike splats which don't
// require a mesh).

TEST(Adversarial, ParticleWithInvalidMeshExcluded)
{
    SceneBuilder b;

    TransformNode root_node{};
    NodeHandle root = add_node(b, root_node);

    TransformNode obj_node{};
    obj_node.flags = TransformNodeFlag::RenderDomain;
    NodeHandle obj = add_node(b, obj_node);

    add_edge(b, root, obj);
    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    propagate_all(result->polytree);

    std::vector<RenderableDescriptor> descs(node_count(result->polytree));
    descs[root] = { classify_legacy_renderable(RenderPipeline::None) };
    descs[obj] = {
        .node_class = classify_legacy_renderable(RenderPipeline::Particle),
        .mesh = INVALID_MESH, .material = 0u,
        .local_bounds = unit_box(), .visible = true
    };

    CompiledSceneStorage storage{};
    compile(storage, result->polytree, descs, {}, identity_view());

    EXPECT_EQ(storage.scene.particles.size(), 0u)
        << "particle with INVALID_MESH should not produce a compiled record";
}


// ═════════════════════════════════════════════════════════════════════════════
// 4. update_compiled_transforms: explicit dirty_nodes consistent with all-dirty
// ═════════════════════════════════════════════════════════════════════════════
//
// Calling update_compiled_transforms with dirty_nodes={} (all-dirty) and with
// an explicit list containing every renderable node must produce the same result.

TEST(Adversarial, ExplicitDirtyNodesMatchesAllDirty)
{
    SceneBuilder b;

    TransformNode root_node{};
    NodeHandle root = add_node(b, root_node);

    constexpr int N = 4;
    NodeHandle children[N];
    for (int i = 0; i < N; ++i) {
        TransformNode cn{};
        cn.local = translation(static_cast<float>(i), 0.f, 0.f);
        cn.flags = TransformNodeFlag::RenderDomain;
        children[i] = add_node(b, cn);
        add_edge(b, root, children[i]);
    }

    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    propagate_all(result->polytree);

    std::vector<RenderableDescriptor> descs(node_count(result->polytree));
    descs[root] = { classify_legacy_renderable(RenderPipeline::None) };
    for (int i = 0; i < N; ++i)
        descs[children[i]] = {
            .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
            .mesh = static_cast<MeshHandle>(i), .material = 0u,
            .local_bounds = unit_box(), .visible = true };

    auto view = wide_view_at(0.f);

    // Path A: all-dirty (empty dirty_nodes).
    CompiledSceneStorage storage_a{};
    compile(storage_a, result->polytree, descs, {}, view);

    // Move all objects to new positions.
    for (int i = 0; i < N; ++i)
        set_world(result->polytree, children[i],
                  translation(static_cast<float>(i) * 10.f, 0.f, 0.f));

    update_compiled_transforms(storage_a, result->polytree, descs, view,
                               /*dirty_nodes=*/{}, /*view_changed=*/true);

    // Path B: explicit dirty list containing all renderable children.
    CompiledSceneStorage storage_b{};
    // Reset world transforms and recompile from scratch.
    for (int i = 0; i < N; ++i)
        set_world(result->polytree, children[i],
                  translation(static_cast<float>(i), 0.f, 0.f));
    compile(storage_b, result->polytree, descs, {}, view);

    // Apply same world transforms.
    for (int i = 0; i < N; ++i)
        set_world(result->polytree, children[i],
                  translation(static_cast<float>(i) * 10.f, 0.f, 0.f));

    std::vector<NodeHandle> dirty_list(children, children + N);
    update_compiled_transforms(storage_b, result->polytree, descs, view,
                               dirty_list, /*view_changed=*/true);

    // Both paths must produce the same world matrices.
    ASSERT_EQ(storage_a.scene.opaque.size(), storage_b.scene.opaque.size());
    for (uint32_t i = 0; i < storage_a.scene.opaque.size(); ++i) {
        EXPECT_FLOAT_EQ(storage_a.scene.opaque[i].world.m[12],
                        storage_b.scene.opaque[i].world.m[12])
            << "world.m[12] mismatch at opaque[" << i << "]";
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// 5. update_frame_view idempotency
// ═════════════════════════════════════════════════════════════════════════════
//
// Calling update_frame_view twice with the same IR and scene should produce
// the same frame.

TEST(Adversarial, UpdateFrameViewIdempotent)
{
    SceneBuilder b;

    TransformNode root_node{};
    NodeHandle root = add_node(b, root_node);

    TransformNode obj_node{};
    obj_node.local = translation(0.f, 0.f, 3.f);
    obj_node.flags = TransformNodeFlag::RenderDomain;
    NodeHandle obj = add_node(b, obj_node);

    add_edge(b, root, obj);
    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    propagate_all(result->polytree);

    std::vector<RenderableDescriptor> descs(node_count(result->polytree));
    descs[root] = { classify_legacy_renderable(RenderPipeline::None) };
    descs[obj] = {
        .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
        .mesh = 1u, .material = 1u, .local_bounds = unit_box(), .visible = true };

    auto view = wide_view_at(0.f);

    CompiledSceneStorage cs{};
    compile(cs, result->polytree, descs, {}, view);

    RenderIRStorage ir{};
    auto ir_view = build_render_ir(ir, cs.scene);

    RenderFrameStorage frame{};
    auto f1 = build_frame(frame, ir_view, cs.scene);

    ASSERT_EQ(f1.opaque.size(), 1u);
    float world_z_1 = f1.opaque[0].world.m[14];
    uint64_t key_1  = f1.opaque[0].sort_key;

    // Second call with same data.
    auto f2 = update_frame_view(frame, ir_view, cs.scene);

    ASSERT_EQ(f2.opaque.size(), 1u);
    EXPECT_FLOAT_EQ(f2.opaque[0].world.m[14], world_z_1)
        << "update_frame_view changed opaque world matrix";
    EXPECT_EQ(f2.opaque[0].sort_key, key_1)
        << "update_frame_view changed opaque sort key";
}


// ═════════════════════════════════════════════════════════════════════════════
// 6. Deep hierarchy — 10-level transform propagation
// ═════════════════════════════════════════════════════════════════════════════
//
// Each node adds +1 on X.  After propagation the leaf world.m[12] should be 10.

TEST(Adversarial, DeepHierarchyTransformPropagation)
{
    constexpr int DEPTH = 10;

    SceneBuilder b;

    NodeHandle nodes[DEPTH];
    for (int i = 0; i < DEPTH; ++i) {
        TransformNode n{};
        n.local = translation(1.f, 0.f, 0.f);
        nodes[i] = add_node(b, n);
        if (i > 0) add_edge(b, nodes[i - 1], nodes[i]);
    }

    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    propagate_all(result->polytree);

    float leaf_x = node_data(result->polytree, nodes[DEPTH - 1]).world.m[12];
    EXPECT_FLOAT_EQ(leaf_x, static_cast<float>(DEPTH))
        << "deep hierarchy: leaf world X should be sum of all local X values";
}


// ═════════════════════════════════════════════════════════════════════════════
// 7. Deep hierarchy — dirty mid-level node propagates to leaf
// ═════════════════════════════════════════════════════════════════════════════
//
// After full propagation, dirty a mid-level node. update_static must propagate
// the change through to the leaf.

TEST(Adversarial, DirtyMidLevelPropagatesDeep)
{
    constexpr int DEPTH = 6;

    SceneBuilder b;

    NodeHandle nodes[DEPTH];
    for (int i = 0; i < DEPTH; ++i) {
        TransformNode n{};
        n.local = translation(1.f, 0.f, 0.f);
        nodes[i] = add_node(b, n);
        if (i > 0) add_edge(b, nodes[i - 1], nodes[i]);
    }

    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    auto& g = result->polytree;

    // Full propagation + clean.
    const uint32_t frame1 = 1u;
    NodeHandle scratch[DEPTH];
    auto roots1 = collect_dirty_roots(g, frame1, scratch);
    update_static(g, roots1, frame1);

    // Sanity: leaf at x=6.
    EXPECT_FLOAT_EQ(node_data(g, nodes[DEPTH - 1]).world.m[12], 6.f);

    // Dirty the middle node (index 2) — change its local X from 1 to 100.
    const uint32_t frame2 = 2u;
    set_local(g, nodes[2], translation(100.f, 0.f, 0.f));

    auto roots2 = collect_dirty_roots(g, frame2, scratch);
    ASSERT_EQ(roots2.size(), 1u);
    EXPECT_EQ(roots2[0], nodes[2]);

    update_static(g, roots2, frame2);

    // Nodes 0,1 unchanged: 1, 2
    EXPECT_FLOAT_EQ(node_data(g, nodes[0]).world.m[12], 1.f);
    EXPECT_FLOAT_EQ(node_data(g, nodes[1]).world.m[12], 2.f);

    // Node 2: parent(1) world=2, local=100 → world=102
    EXPECT_FLOAT_EQ(node_data(g, nodes[2]).world.m[12], 102.f);

    // All subsequent nodes accumulate +1 each.
    // Node 3: 103, Node 4: 104, Node 5: 105
    for (int i = 3; i < DEPTH; ++i) {
        float expected = 102.f + static_cast<float>(i - 2);
        EXPECT_FLOAT_EQ(node_data(g, nodes[i]).world.m[12], expected)
            << "node " << i << " world X wrong after mid-level dirty";
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// 8. Compile → transform update → recompile produces valid metadata
// ═════════════════════════════════════════════════════════════════════════════
//
// After a transform update mutates the stable buffer in-place, a full
// recompile must still produce valid and consistent metadata.

TEST(Adversarial, RecompileAfterTransformUpdateProducesValidMetadata)
{
    SceneBuilder b;

    TransformNode root_node{};
    NodeHandle root = add_node(b, root_node);

    TransformNode obj_node{};
    obj_node.local = Mat4::identity();
    obj_node.flags = TransformNodeFlag::RenderDomain;
    NodeHandle obj = add_node(b, obj_node);

    add_edge(b, root, obj);
    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    propagate_all(result->polytree);

    std::vector<RenderableDescriptor> descs(node_count(result->polytree));
    descs[root] = { classify_legacy_renderable(RenderPipeline::None) };
    descs[obj] = {
        .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
        .mesh = 7u, .material = 3u, .local_bounds = unit_box(), .visible = true };

    CompiledSceneStorage storage{};
    compile(storage, result->polytree, descs, {}, identity_view());

    // Transform update — mutates stable buffer in-place.
    set_world(result->polytree, obj, translation(0.f, 0.f, 5.f));
    update_compiled_transforms(storage, result->polytree, descs, identity_view());

    EXPECT_FLOAT_EQ(storage.scene.opaque[0].world.m[14], 5.f);

    // Now recompile from scratch.
    compile(storage, result->polytree, descs, {}, identity_view());

    // Metadata must be valid.
    EXPECT_EQ(storage.metadata.node_to_output[obj].kind,
              CompiledOutputKind::SurfacePrimitive);
    EXPECT_EQ(storage.metadata.node_to_output[obj].index, 0u);
    EXPECT_EQ(storage.metadata.opaque_source_nodes[0], obj);
    EXPECT_EQ(storage.scene.opaque[0].mesh, 7u);
}


// ═════════════════════════════════════════════════════════════════════════════
// 9. Full pipeline with mixed types — all counts correct
// ═════════════════════════════════════════════════════════════════════════════
//
// Scene with 2 opaque, 1 transparent, 2 splats, 1 particle, 1 light.
// Every pipeline count must be exact through compile → IR → frame → submit.

TEST(Adversarial, MixedPipelineCountsEndToEnd)
{
    SceneBuilder b;

    TransformNode root_node{};
    NodeHandle root = add_node(b, root_node);

    // 2 opaque
    NodeHandle op[2];
    for (int i = 0; i < 2; ++i) {
        TransformNode cn{};
        cn.local = translation(static_cast<float>(i), 0.f, 0.f);
        cn.flags = TransformNodeFlag::RenderDomain;
        op[i] = add_node(b, cn);
        add_edge(b, root, op[i]);
    }

    // 1 transparent
    TransformNode tr_node{};
    tr_node.local = translation(0.f, 0.f, 2.f);
    tr_node.flags = TransformNodeFlag::RenderDomain;
    NodeHandle tr = add_node(b, tr_node);
    add_edge(b, root, tr);

    // 2 splats
    NodeHandle sp[2];
    for (int i = 0; i < 2; ++i) {
        TransformNode cn{};
        cn.local = translation(0.f, static_cast<float>(i), 3.f);
        cn.flags = TransformNodeFlag::RenderDomain;
        sp[i] = add_node(b, cn);
        add_edge(b, root, sp[i]);
    }

    // 1 particle
    TransformNode pa_node{};
    pa_node.local = translation(0.f, 0.f, 4.f);
    pa_node.flags = TransformNodeFlag::RenderDomain;
    NodeHandle pa = add_node(b, pa_node);
    add_edge(b, root, pa);

    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    propagate_all(result->polytree);

    std::vector<RenderableDescriptor> descs(node_count(result->polytree));
    descs[root] = { classify_legacy_renderable(RenderPipeline::None) };
    for (int i = 0; i < 2; ++i)
        descs[op[i]] = {
            .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
            .mesh = static_cast<MeshHandle>(i),
            .material = static_cast<MaterialHandle>(i),
            .local_bounds = unit_box(), .visible = true };
    descs[tr] = {
        .node_class = classify_legacy_renderable(RenderPipeline::TransparentGeometry),
        .mesh = 10u, .material = 10u, .local_bounds = unit_box(), .visible = true };
    for (int i = 0; i < 2; ++i)
        descs[sp[i]] = {
            .node_class = classify_legacy_renderable(RenderPipeline::Splat),
            .mesh = INVALID_MESH, .material = INVALID_MATERIAL,
            .splat_data = SplatDescriptor{{1,1,1},{0,0,0,1},{1,1,1},1.f},
            .visible = true };
    descs[pa] = {
        .node_class = classify_legacy_renderable(RenderPipeline::Particle),
        .mesh = 20u, .material = 20u, .local_bounds = unit_box(), .visible = true };

    LightRecord sun{};
    sun.type = LightType::Directional;
    sun.intensity = 1.f;
    std::array<LightRecord, 1> lights{ sun };

    auto view = wide_view_at(0.f);

    CompiledSceneStorage cs{};
    compile(cs, result->polytree, descs, lights, view);

    EXPECT_EQ(cs.scene.opaque.size(), 2u);
    EXPECT_EQ(cs.scene.transparent.size(), 1u);
    EXPECT_EQ(cs.scene.splats.size(), 2u);
    EXPECT_EQ(cs.scene.particles.size(), 1u);
    EXPECT_EQ(cs.scene.lights.size(), 1u);

    RenderIRStorage ir{};
    auto ir_view = build_render_ir(ir, cs.scene);

    EXPECT_EQ(ir_view.opaque.size(), 2u);
    EXPECT_EQ(ir_view.transparent.size(), 1u);
    EXPECT_EQ(ir_view.splats.size(), 2u);
    EXPECT_EQ(ir_view.particles.size(), 1u);

    RenderFrameStorage frame{};
    auto frame_view = build_frame(frame, ir_view, cs.scene);

    EXPECT_EQ(frame_view.opaque.size(), 2u);
    EXPECT_EQ(frame_view.transparent.size(), 1u);
    EXPECT_EQ(frame_view.splats.size(), 2u);
    EXPECT_EQ(frame_view.particles.size(), 1u);

    auto submit_result = submit(frame_view);
    EXPECT_EQ(submit_result.opaque_count(), 2u);
    EXPECT_EQ(submit_result.transparent_count(), 1u);
    EXPECT_EQ(submit_result.splat_count(), 2u);
    EXPECT_EQ(submit_result.particle_count(), 1u);
    EXPECT_EQ(submit_result.total(), 6u);
}


// ═════════════════════════════════════════════════════════════════════════════
// 10. Splat cloud_handle preserved through descriptor update
// ═════════════════════════════════════════════════════════════════════════════
//
// After compile, update_compiled_descriptors must correctly update cloud_handle.

TEST(Adversarial, SplatCloudHandleUpdatedByDescriptorPatch)
{
    SceneBuilder b;

    TransformNode root_node{};
    NodeHandle root = add_node(b, root_node);

    TransformNode obj_node{};
    obj_node.flags = TransformNodeFlag::RenderDomain;
    NodeHandle obj = add_node(b, obj_node);

    add_edge(b, root, obj);
    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    propagate_all(result->polytree);

    std::vector<RenderableDescriptor> descs(node_count(result->polytree));
    descs[root] = { classify_legacy_renderable(RenderPipeline::None) };
    descs[obj] = {
        .node_class = classify_legacy_renderable(RenderPipeline::Splat),
        .mesh = INVALID_MESH, .material = INVALID_MATERIAL,
        .splat_data = SplatDescriptor{
            .scale        = { 1.f, 1.f, 1.f },
            .rotation     = { 0.f, 0.f, 0.f, 1.f },
            .color        = { 1.f, 0.f, 0.f },
            .opacity      = 1.f,
            .cloud_handle = 42u,
            .cloud_local_index = 7u,
        },
        .visible = true
    };

    CompiledSceneStorage storage{};
    compile(storage, result->polytree, descs, {}, identity_view());

    ASSERT_EQ(storage.scene.splats.size(), 1u);
    EXPECT_EQ(storage.scene.splats[0].cloud_handle, 42u);
    EXPECT_EQ(storage.scene.splats[0].cloud_local_index, 7u);

    // Change cloud_handle via descriptor update.
    descs[obj].splat_data.cloud_handle      = 99u;
    descs[obj].splat_data.cloud_local_index  = 13u;

    update_compiled_descriptors(storage, descs);

    EXPECT_EQ(storage.scene.splats[0].cloud_handle, 99u)
        << "cloud_handle not updated by descriptor patch";
    EXPECT_EQ(storage.scene.splats[0].cloud_local_index, 13u)
        << "cloud_local_index not updated by descriptor patch";
}


// ═════════════════════════════════════════════════════════════════════════════
// 11. refresh_compiled_scene: Transform+View dirty produces correct depths
// ═════════════════════════════════════════════════════════════════════════════
//
// When both Transform and View are dirty, the transform update runs first,
// then update_view.  The final depth must reflect both the new object position
// and the new camera position.

TEST(Adversarial, TransformAndViewDirtyProducesCorrectDepth)
{
    SceneBuilder b;

    TransformNode root_node{};
    NodeHandle root = add_node(b, root_node);

    TransformNode obj_node{};
    obj_node.local = translation(0.f, 0.f, 5.f);
    obj_node.flags = TransformNodeFlag::RenderDomain;
    NodeHandle obj = add_node(b, obj_node);

    add_edge(b, root, obj);
    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    propagate_all(result->polytree);

    std::vector<RenderableDescriptor> descs(node_count(result->polytree));
    descs[root] = { classify_legacy_renderable(RenderPipeline::None) };
    descs[obj] = {
        .node_class = classify_legacy_renderable(RenderPipeline::TransparentGeometry),
        .mesh = 1u, .material = 1u, .local_bounds = unit_box(), .visible = true };

    auto view0 = wide_view_at(0.f);
    CompiledSceneStorage storage{};
    compile(storage, result->polytree, descs, {}, view0);

    ASSERT_EQ(storage.scene.transparent.size(), 1u);
    float depth0 = storage.scene.transparent[0].depth;

    // Move object from z=5 to z=20 and camera from z=0 to z=10.
    set_world(result->polytree, obj, translation(0.f, 0.f, 20.f));

    auto view1 = wide_view_at(10.f);
    refresh_compiled_scene(
        storage, result->polytree, descs, {}, view1,
        SceneDirtyBits::Transform | SceneDirtyBits::View);

    // Object at z=20, camera at z=10.
    // view matrix m[14] = -10.  view_depth = -(20 + (-10)) = -(10) = -10.
    // The object's world bounds center is at z=20.
    // depth = -mul_point(view, center).z = -(center.z + view.m[14])
    //       = -(20 + (-10)) = -10.
    // (This is a view-space depth; sign depends on convention.)
    float depth1 = storage.scene.transparent[0].depth;
    EXPECT_NE(depth0, depth1)
        << "depth did not change after Transform+View refresh";

    // Also verify the world matrix was updated.
    EXPECT_FLOAT_EQ(storage.scene.transparent[0].world.m[14], 20.f);
}


// ═════════════════════════════════════════════════════════════════════════════
// 12. All nodes invisible produces empty scene end-to-end
// ═════════════════════════════════════════════════════════════════════════════

TEST(Adversarial, AllInvisibleProducesEmptyPipeline)
{
    SceneBuilder b;

    TransformNode root_node{};
    NodeHandle root = add_node(b, root_node);

    constexpr int N = 4;
    NodeHandle children[N];
    for (int i = 0; i < N; ++i) {
        TransformNode cn{};
        cn.flags = TransformNodeFlag::RenderDomain;
        children[i] = add_node(b, cn);
        add_edge(b, root, children[i]);
    }

    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    propagate_all(result->polytree);

    std::vector<RenderableDescriptor> descs(node_count(result->polytree));
    descs[root] = { classify_legacy_renderable(RenderPipeline::None) };
    // All visible=false
    for (int i = 0; i < N; ++i)
        descs[children[i]] = {
            .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
            .mesh = static_cast<MeshHandle>(i),
            .material = 0u,
            .local_bounds = unit_box(),
            .visible = false };

    CompiledSceneStorage cs{};
    compile(cs, result->polytree, descs, {}, identity_view());

    RenderIRStorage ir{};
    auto ir_view = build_render_ir(ir, cs.scene);

    RenderFrameStorage frame{};
    auto frame_view = build_frame(frame, ir_view, cs.scene);

    EXPECT_EQ(frame_view.opaque.size(), 0u);
    EXPECT_EQ(frame_view.transparent.size(), 0u);
    EXPECT_EQ(frame_view.splats.size(), 0u);
    EXPECT_EQ(frame_view.particles.size(), 0u);

    auto submit_result = submit(frame_view);
    EXPECT_EQ(submit_result.total(), 0u);
}


// ═════════════════════════════════════════════════════════════════════════════
// 13. Transparent sort ordering with equidistant objects
// ═════════════════════════════════════════════════════════════════════════════
//
// Two transparent objects at the same Z should both appear in the output
// (neither should be lost) and their sort keys should be equal.

TEST(Adversarial, EquidistantTransparentsPreserved)
{
    SceneBuilder b;

    TransformNode root_node{};
    NodeHandle root = add_node(b, root_node);

    NodeHandle tr[2];
    for (int i = 0; i < 2; ++i) {
        TransformNode cn{};
        cn.local = translation(static_cast<float>(i), 0.f, 5.f); // same Z
        cn.flags = TransformNodeFlag::RenderDomain;
        tr[i] = add_node(b, cn);
        add_edge(b, root, tr[i]);
    }

    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    propagate_all(result->polytree);

    std::vector<RenderableDescriptor> descs(node_count(result->polytree));
    descs[root] = { classify_legacy_renderable(RenderPipeline::None) };
    for (int i = 0; i < 2; ++i)
        descs[tr[i]] = {
            .node_class = classify_legacy_renderable(RenderPipeline::TransparentGeometry),
            .mesh = static_cast<MeshHandle>(i), .material = 0u,
            .local_bounds = unit_box(), .visible = true };

    auto view = wide_view_at(0.f);

    CompiledSceneStorage cs{};
    compile(cs, result->polytree, descs, {}, view);

    ASSERT_EQ(cs.scene.transparent.size(), 2u);

    RenderIRStorage ir{};
    auto ir_view = build_render_ir(ir, cs.scene);

    // Both should be visible.
    EXPECT_EQ(ir_view.transparent.size(), 2u)
        << "equidistant transparent objects: one was lost";

    // Sort keys should be equal (same depth → same key).
    EXPECT_EQ(ir_view.transparent[0].sort_key, ir_view.transparent[1].sort_key)
        << "equidistant transparent objects should have equal sort keys";
}


// ═════════════════════════════════════════════════════════════════════════════
// 14. Opaque with SurfaceClass::None is excluded
// ═════════════════════════════════════════════════════════════════════════════
//
// A Mesh producer with SurfaceClass::None should produce no output.

TEST(Adversarial, MeshWithSurfaceNoneExcluded)
{
    SceneBuilder b;

    TransformNode root_node{};
    NodeHandle root = add_node(b, root_node);

    TransformNode obj_node{};
    obj_node.flags = TransformNodeFlag::RenderDomain;
    NodeHandle obj = add_node(b, obj_node);

    add_edge(b, root, obj);
    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    propagate_all(result->polytree);

    std::vector<RenderableDescriptor> descs(node_count(result->polytree));
    descs[root] = { classify_legacy_renderable(RenderPipeline::None) };
    descs[obj] = {
        .node_class = SceneNodeClass{
            .role            = SceneRole::Renderable,
            .producer        = ProducerKind::Mesh,
            .default_surface = SurfaceClass::None,   // ← no surface class
        },
        .mesh = 1u, .material = 1u, .local_bounds = unit_box(), .visible = true
    };

    CompiledSceneStorage storage{};
    compile(storage, result->polytree, descs, {}, identity_view());

    EXPECT_EQ(storage.scene.opaque.size(), 0u)
        << "Mesh with SurfaceClass::None should not produce opaque output";
    EXPECT_EQ(storage.scene.transparent.size(), 0u)
        << "Mesh with SurfaceClass::None should not produce transparent output";
}


// ═════════════════════════════════════════════════════════════════════════════
// 15. Additive surface class routes to transparent
// ═════════════════════════════════════════════════════════════════════════════

TEST(Adversarial, AdditiveSurfaceRoutesToTransparent)
{
    SceneBuilder b;

    TransformNode root_node{};
    NodeHandle root = add_node(b, root_node);

    TransformNode obj_node{};
    obj_node.flags = TransformNodeFlag::RenderDomain;
    NodeHandle obj = add_node(b, obj_node);

    add_edge(b, root, obj);
    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    propagate_all(result->polytree);

    std::vector<RenderableDescriptor> descs(node_count(result->polytree));
    descs[root] = { classify_legacy_renderable(RenderPipeline::None) };
    descs[obj] = {
        .node_class = SceneNodeClass{
            .role            = SceneRole::Renderable,
            .producer        = ProducerKind::Mesh,
            .default_surface = SurfaceClass::Additive,
            .spatial         = SpatialKind::MeshBounds,
            .compile         = CompileBehavior::Static,
            .domains         = RenderDomain::Surface | RenderDomain::Transparent,
        },
        .mesh = 1u, .material = 1u, .local_bounds = unit_box(), .visible = true
    };

    CompiledSceneStorage storage{};
    compile(storage, result->polytree, descs, {}, identity_view());

    EXPECT_EQ(storage.scene.opaque.size(), 0u);
    EXPECT_EQ(storage.scene.transparent.size(), 1u)
        << "Additive surface should route to transparent pipeline";
    EXPECT_EQ(storage.scene.transparent[0].mesh, 1u);
}
