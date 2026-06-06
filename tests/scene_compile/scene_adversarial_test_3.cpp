// tests/scene_compile/scene_adversarial_test_3.cpp
//
// Third batch of adversarial tests — stress-testing the scene-render pipeline
// ahead of collision/physics frame product work.
//
// Focus areas:
//   - Wide hierarchies (many siblings under one parent)
//   - Deep hierarchies under transform mutation
//   - Storage reuse across grow/shrink cycles at every pipeline layer
//   - Root-only and zero-renderable scene robustness
//   - Mixed pipeline partial-visibility patterns
//   - Compiled transform correctness with scale and rotation
//   - Full pipeline round-trip with all-culled frustum

#include <gtest/gtest.h>

#include <math/mat4.h>
#include <math/math_types.h>
#include <render/ir/render_ir.h>
#include <render/frame/render_frame.h>
#include <render/backend/stub_backend.h>
#include <scene/compile/scene_compiler.h>
#include <scene/compile/legacy_classification.h>
#include <scene/scene_graph.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <vector>

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

    // Uniform scale matrix.
    Mat4 scale_mat(float s)
    {
        Mat4 m = Mat4::identity();
        m.m[0] = s; m.m[5] = s; m.m[10] = s;
        return m;
    }

    // 90-degree rotation around Y axis.
    Mat4 rotate_y_90()
    {
        Mat4 m = Mat4::identity();
        m.m[0]  =  0.f; m.m[8]  = 1.f;
        m.m[2]  = -1.f; m.m[10] = 0.f;
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

    struct SimpleScene {
        PolytreeStorage<TransformNode, SceneEdge> storage;
        NodeHandle root;

        SceneGraph&       graph()       { return storage.polytree; }
        const SceneGraph& graph() const { return storage.polytree; }
    };

    SimpleScene make_scene(int n_children, std::vector<NodeHandle>& children)
    {
        SceneBuilder b;

        TransformNode root_node{};
        NodeHandle root = add_node(b, root_node);

        children.resize(n_children);
        for (int i = 0; i < n_children; ++i) {
            TransformNode cn{};
            cn.local = Mat4::identity();
            cn.flags = TransformNodeFlag::RenderDomain;
            children[i] = add_node(b, cn);
            add_edge(b, root, children[i]);
        }

        auto result = build(std::move(b));
        propagate_all(result->polytree);

        return { std::move(*result), root };
    }

} // namespace


// ═════════════════════════════════════════════════════════════════════════════
// 1. Wide hierarchy — 100 siblings under one root
// ═════════════════════════════════════════════════════════════════════════════
//
// Stress-test that scene compile, IR, and frame handle a flat-wide scene
// correctly.  All 100 opaque nodes must survive end-to-end.

TEST(Adversarial3, WideHierarchy100Siblings)
{
    constexpr int N = 100;
    std::vector<NodeHandle> ch;
    auto scene = make_scene(N, ch);

    for (int i = 0; i < N; ++i)
        set_world(scene.graph(), ch[i],
                  translation(static_cast<float>(i) * 0.1f, 0.f, 0.f));

    std::vector<RenderableDescriptor> descs(node_count(scene.graph()));
    descs[scene.root] = { classify_legacy_renderable(RenderPipeline::None) };
    for (int i = 0; i < N; ++i)
        descs[ch[i]] = {
            classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
            static_cast<MeshHandle>(i), 0u, unit_box(), {}, true };

    auto view = wide_view_at(0.f);

    CompiledSceneStorage cs{};
    compile(cs, scene.graph(), descs, {}, view);
    ASSERT_EQ(cs.scene.opaque.size(), static_cast<size_t>(N));

    // Metadata: every node has a forward lookup.
    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(cs.metadata.node_to_output[ch[i]].kind,
                  CompiledOutputKind::SurfacePrimitive)
            << "node " << i << " missing compiled output";
    }

    RenderIRStorage ir{};
    auto ir_view = build_render_ir(ir, cs.scene);
    EXPECT_EQ(ir_view.opaque.size(), static_cast<size_t>(N));

    RenderFrameStorage frame{};
    auto frame_view = build_frame(frame, ir_view, cs.scene);
    EXPECT_EQ(frame_view.opaque.size(), static_cast<size_t>(N));

    auto submit_result = submit(frame_view);
    EXPECT_EQ(submit_result.opaque_count(), static_cast<uint32_t>(N));
}


// ═════════════════════════════════════════════════════════════════════════════
// 2. Root-only scene — zero renderable nodes
// ═════════════════════════════════════════════════════════════════════════════
//
// A scene with only a root node and no children.  The full pipeline must
// produce empty output without crashing.

TEST(Adversarial3, RootOnlySceneProducesEmptyOutput)
{
    SceneBuilder b;
    TransformNode root_node{};
    NodeHandle root = add_node(b, root_node);

    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    propagate_all(result->polytree);

    std::vector<RenderableDescriptor> descs(node_count(result->polytree));
    descs[root] = { classify_legacy_renderable(RenderPipeline::None) };

    CompiledSceneStorage cs{};
    compile(cs, result->polytree, descs, {}, identity_view());

    EXPECT_EQ(cs.scene.opaque.size(), 0u);
    EXPECT_EQ(cs.scene.transparent.size(), 0u);
    EXPECT_EQ(cs.scene.splats.size(), 0u);
    EXPECT_EQ(cs.scene.particles.size(), 0u);
    EXPECT_EQ(cs.scene.lights.size(), 0u);

    RenderIRStorage ir{};
    auto ir_view = build_render_ir(ir, cs.scene);
    EXPECT_EQ(ir_view.opaque.size(), 0u);

    RenderFrameStorage frame{};
    auto frame_view = build_frame(frame, ir_view, cs.scene);
    EXPECT_EQ(frame_view.opaque.size(), 0u);

    // update paths should also tolerate empty.
    update_view(cs, identity_view());
    update_render_ir(ir, cs.scene);
    auto f2 = update_frame_view(frame, ir.ir, cs.scene);
    EXPECT_EQ(f2.opaque.size(), 0u);
}


// ═════════════════════════════════════════════════════════════════════════════
// 3. All objects outside frustum — full pipeline produces zero draw commands
// ═════════════════════════════════════════════════════════════════════════════
//
// 5 opaque objects, all at z=50 (well outside identity [-1,1] frustum).
// compile() includes them, but IR must cull all.  Frame must be empty.

TEST(Adversarial3, AllCulledProducesEmptyFrame)
{
    constexpr int N = 5;
    std::vector<NodeHandle> ch;
    auto scene = make_scene(N, ch);

    for (int i = 0; i < N; ++i)
        set_world(scene.graph(), ch[i],
                  translation(static_cast<float>(i), 0.f, 50.f));

    std::vector<RenderableDescriptor> descs(node_count(scene.graph()));
    descs[scene.root] = { classify_legacy_renderable(RenderPipeline::None) };
    for (int i = 0; i < N; ++i)
        descs[ch[i]] = {
            classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
            static_cast<MeshHandle>(i), 0u, unit_box(), {}, true };

    CompiledSceneStorage cs{};
    compile(cs, scene.graph(), descs, {}, identity_view());
    ASSERT_EQ(cs.scene.opaque.size(), static_cast<size_t>(N));

    RenderIRStorage ir{};
    auto ir_view = build_render_ir(ir, cs.scene);
    EXPECT_EQ(ir_view.opaque.size(), 0u);
    EXPECT_EQ(ir_view.culling.culled_opaque, static_cast<uint32_t>(N));

    RenderFrameStorage frame{};
    auto frame_view = build_frame(frame, ir_view, cs.scene);
    EXPECT_EQ(frame_view.opaque.size(), 0u);

    auto submit_result = submit(frame_view);
    EXPECT_EQ(submit_result.total(), 0u);
}


// ═════════════════════════════════════════════════════════════════════════════
// 4. Mixed pipeline with partial visibility — only visible nodes compile
// ═════════════════════════════════════════════════════════════════════════════
//
// 3 opaque (2 visible, 1 hidden), 2 transparent (1 visible, 1 hidden),
// 1 splat (visible), 1 particle (hidden).
// Compiled counts must match visible counts exactly.

TEST(Adversarial3, MixedPipelinePartialVisibility)
{
    constexpr int TOTAL = 7;
    std::vector<NodeHandle> ch;
    auto scene = make_scene(TOTAL, ch);

    for (int i = 0; i < TOTAL; ++i)
        set_world(scene.graph(), ch[i],
                  translation(static_cast<float>(i), 0.f, 0.f));

    std::vector<RenderableDescriptor> descs(node_count(scene.graph()));
    descs[scene.root] = { classify_legacy_renderable(RenderPipeline::None) };

    // Opaque: ch[0] visible, ch[1] visible, ch[2] hidden
    descs[ch[0]] = { classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
                     0u, 0u, unit_box(), {}, true };
    descs[ch[1]] = { classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
                     1u, 0u, unit_box(), {}, true };
    descs[ch[2]] = { classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
                     2u, 0u, unit_box(), {}, false };

    // Transparent: ch[3] visible, ch[4] hidden
    descs[ch[3]] = { classify_legacy_renderable(RenderPipeline::TransparentGeometry),
                     3u, 0u, unit_box(), {}, true };
    descs[ch[4]] = { classify_legacy_renderable(RenderPipeline::TransparentGeometry),
                     4u, 0u, unit_box(), {}, false };

    // Splat: ch[5] visible
    descs[ch[5]] = { classify_legacy_renderable(RenderPipeline::Splat),
                     INVALID_MESH, INVALID_MATERIAL, {},
                     SplatDescriptor{}, true };

    // Particle: ch[6] hidden
    descs[ch[6]] = { classify_legacy_renderable(RenderPipeline::Particle),
                     6u, 0u, unit_box(), {}, false };

    auto view = wide_view_at(0.f);
    CompiledSceneStorage cs{};
    compile(cs, scene.graph(), descs, {}, view);

    EXPECT_EQ(cs.scene.opaque.size(), 2u);
    EXPECT_EQ(cs.scene.transparent.size(), 1u);
    EXPECT_EQ(cs.scene.splats.size(), 1u);
    EXPECT_EQ(cs.scene.particles.size(), 0u);

    // Hidden nodes must have CompiledOutputKind::None.
    EXPECT_EQ(cs.metadata.node_to_output[ch[2]].kind, CompiledOutputKind::None);
    EXPECT_EQ(cs.metadata.node_to_output[ch[4]].kind, CompiledOutputKind::None);
    EXPECT_EQ(cs.metadata.node_to_output[ch[6]].kind, CompiledOutputKind::None);
}


// ═════════════════════════════════════════════════════════════════════════════
// 5. Deep hierarchy (50 levels) with transform propagation
// ═════════════════════════════════════════════════════════════════════════════
//
// 50-node chain, each adding +0.1 on X.  After propagation, leaf world X
// should be 5.0.  Then dirty the root and reprop — leaf should reflect the
// change.

TEST(Adversarial3, DeepHierarchy50LevelPropagation)
{
    constexpr int DEPTH = 50;

    SceneBuilder b;

    NodeHandle nodes[DEPTH];
    for (int i = 0; i < DEPTH; ++i) {
        TransformNode n{};
        n.local = translation(0.1f, 0.f, 0.f);
        nodes[i] = add_node(b, n);
        if (i > 0) add_edge(b, nodes[i - 1], nodes[i]);
    }

    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    auto& g = result->polytree;

    propagate_all(g);

    float leaf_x = node_data(g, nodes[DEPTH - 1]).world.m[12];
    EXPECT_NEAR(leaf_x, 5.0f, 1e-4f)
        << "50-level deep chain: leaf X should be 50 * 0.1 = 5.0";

    // Dirty the root: shift it +100 on X.
    const uint32_t frame1 = 1u;
    set_local(g, nodes[0], translation(100.1f, 0.f, 0.f));

    NodeHandle scratch[DEPTH];
    auto dirty = collect_dirty_roots(g, frame1, scratch);
    update_static(g, dirty, frame1);

    float new_leaf_x = node_data(g, nodes[DEPTH - 1]).world.m[12];
    EXPECT_NEAR(new_leaf_x, 105.0f, 1e-4f)
        << "after shifting root +100, leaf should be 100.1 + 49*0.1 = 105.0";
}


// ═════════════════════════════════════════════════════════════════════════════
// 6. Scale transform propagates through hierarchy correctly
// ═════════════════════════════════════════════════════════════════════════════
//
// Parent at scale 2, child at translation (1,0,0).
// World position of child should be (2,0,0) due to parent scale.

TEST(Adversarial3, ScaleTransformPropagation)
{
    SceneBuilder b;

    TransformNode parent_n{};
    parent_n.local = scale_mat(2.0f);
    NodeHandle parent_h = add_node(b, parent_n);

    TransformNode child_n{};
    child_n.local = translation(1.f, 0.f, 0.f);
    child_n.flags = TransformNodeFlag::RenderDomain;
    NodeHandle child_h = add_node(b, child_n);

    add_edge(b, parent_h, child_h);

    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    propagate_all(result->polytree);

    const auto& child_world = node_data(result->polytree, child_h).world;

    // world = parent_scale * child_translation
    // Column 3 (translation) = scale(2) * (1,0,0) = (2,0,0)
    EXPECT_FLOAT_EQ(child_world.m[12], 2.0f);
    EXPECT_FLOAT_EQ(child_world.m[13], 0.0f);
    EXPECT_FLOAT_EQ(child_world.m[14], 0.0f);

    // The scale should also be carried in the upper 3x3.
    EXPECT_FLOAT_EQ(child_world.m[0], 2.0f)
        << "X scale should be 2 from parent";
}


// ═════════════════════════════════════════════════════════════════════════════
// 7. Rotation transform propagation — child local offset rotated by parent
// ═════════════════════════════════════════════════════════════════════════════
//
// Parent rotated 90 degrees around Y.
// Child at local (0, 0, 3).
// After rotation, world position should be approximately (3, 0, 0).

TEST(Adversarial3, RotationTransformPropagation)
{
    SceneBuilder b;

    TransformNode parent_n{};
    parent_n.local = rotate_y_90();
    NodeHandle parent_h = add_node(b, parent_n);

    TransformNode child_n{};
    child_n.local = translation(0.f, 0.f, 3.f);
    child_n.flags = TransformNodeFlag::RenderDomain;
    NodeHandle child_h = add_node(b, child_n);

    add_edge(b, parent_h, child_h);

    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    propagate_all(result->polytree);

    const auto& child_world = node_data(result->polytree, child_h).world;

    // 90-degree Y rotation: (0,0,3) -> (3,0,0)
    EXPECT_NEAR(child_world.m[12], 3.0f, 1e-5f)
        << "rotated child X should be ~3";
    EXPECT_NEAR(child_world.m[13], 0.0f, 1e-5f);
    EXPECT_NEAR(child_world.m[14], 0.0f, 1e-5f);
}


// ═════════════════════════════════════════════════════════════════════════════
// 8. Compiled transform includes scale in world matrix
// ═════════════════════════════════════════════════════════════════════════════
//
// An opaque object with a non-identity local transform (scale+translation)
// must have the full world matrix in the compiled record, not just position.

TEST(Adversarial3, CompiledTransformPreservesScale)
{
    std::vector<NodeHandle> ch;
    auto scene = make_scene(1, ch);

    // World = scale(3) * translate(1,2,3)
    // In column-major: scale pre-multiplies the translation, so the
    // resulting translation column is (3*1, 3*2, 3*3) = (3, 6, 9).
    Mat4 world = mul(scale_mat(3.0f), translation(1.f, 2.f, 3.f));
    set_world(scene.graph(), ch[0], world);

    std::vector<RenderableDescriptor> descs(node_count(scene.graph()));
    descs[scene.root] = { classify_legacy_renderable(RenderPipeline::None) };
    descs[ch[0]] = {
        classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
        0u, 0u, unit_box(), {}, true };

    auto view = wide_view_at(0.f);
    CompiledSceneStorage cs{};
    compile(cs, scene.graph(), descs, {}, view);

    ASSERT_EQ(cs.scene.opaque.size(), 1u);

    // The compiled world matrix must match what we set.
    const auto& cw = cs.scene.opaque[0].world;
    EXPECT_FLOAT_EQ(cw.m[0], 3.0f) << "scale X not preserved";
    EXPECT_FLOAT_EQ(cw.m[5], 3.0f) << "scale Y not preserved";
    EXPECT_FLOAT_EQ(cw.m[10], 3.0f) << "scale Z not preserved";
    EXPECT_FLOAT_EQ(cw.m[12], 3.0f) << "translation X should be scaled";
    EXPECT_FLOAT_EQ(cw.m[13], 6.0f) << "translation Y should be scaled";
    EXPECT_FLOAT_EQ(cw.m[14], 9.0f) << "translation Z should be scaled";
}


// ═════════════════════════════════════════════════════════════════════════════
// 9. Storage reuse: grow → shrink → grow at every pipeline layer
// ═════════════════════════════════════════════════════════════════════════════
//
// Full pipeline: compile → IR → frame.  Build with 10 objects, then 1, then
// 10 again.  On the third build the storage should reuse its existing
// buffers (no unnecessary reallocation).

TEST(Adversarial3, GrowShrinkGrowStorageReuse)
{
    std::vector<NodeHandle> ch;
    auto scene = make_scene(10, ch);

    for (int i = 0; i < 10; ++i)
        set_world(scene.graph(), ch[i],
                  translation(static_cast<float>(i) * 0.1f, 0.f, 0.f));

    std::vector<RenderableDescriptor> descs(node_count(scene.graph()));
    descs[scene.root] = { classify_legacy_renderable(RenderPipeline::None) };
    for (int i = 0; i < 10; ++i)
        descs[ch[i]] = {
            classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
            static_cast<MeshHandle>(i), 0u, unit_box(), {}, true };

    auto view = wide_view_at(0.f);

    CompiledSceneStorage cs{};
    RenderIRStorage ir{};
    RenderFrameStorage frame{};

    // Pass 1: 10 objects — initial allocation.
    compile(cs, scene.graph(), descs, {}, view);
    build_render_ir(ir, cs.scene);
    build_frame(frame, ir.ir, cs.scene);

    // Record buffer pointers after first full build.
    const auto* cs_stable_1  = cs.stable_buffer.get();
    const auto* ir_buf_1     = ir.buffer.get();
    const auto* frame_stable_1 = frame.stable_buffer.get();

    // Pass 2: shrink to 1 object.
    for (int i = 1; i < 10; ++i)
        descs[ch[i]].visible = false;

    compile(cs, scene.graph(), descs, {}, view);
    EXPECT_EQ(cs.scene.opaque.size(), 1u);
    build_render_ir(ir, cs.scene);
    build_frame(frame, ir.ir, cs.scene);

    // Buffers should be reused (not reallocated) because capacity >= 1.
    EXPECT_EQ(cs.stable_buffer.get(), cs_stable_1)
        << "CompiledSceneStorage should reuse buffer when shrinking";
    EXPECT_EQ(ir.buffer.get(), ir_buf_1)
        << "RenderIRStorage should reuse buffer when shrinking";
    EXPECT_EQ(frame.stable_buffer.get(), frame_stable_1)
        << "RenderFrameStorage should reuse buffer when shrinking";

    // Pass 3: grow back to 10 objects.
    for (int i = 1; i < 10; ++i)
        descs[ch[i]].visible = true;

    compile(cs, scene.graph(), descs, {}, view);
    EXPECT_EQ(cs.scene.opaque.size(), 10u);
    build_render_ir(ir, cs.scene);
    build_frame(frame, ir.ir, cs.scene);

    // Buffers should still be reused — capacity was established in pass 1.
    EXPECT_EQ(cs.stable_buffer.get(), cs_stable_1)
        << "CompiledSceneStorage should reuse buffer when growing back to original size";
    EXPECT_EQ(ir.buffer.get(), ir_buf_1)
        << "RenderIRStorage should reuse buffer when growing back";
    EXPECT_EQ(frame.stable_buffer.get(), frame_stable_1)
        << "RenderFrameStorage should reuse buffer when growing back";
}


// ═════════════════════════════════════════════════════════════════════════════
// 10. Compile → update_view → update_ir → update_frame multi-cycle
// ═════════════════════════════════════════════════════════════════════════════
//
// Simulate 10 camera-move frames using the update paths. The final frame
// must have correct depths and draw counts.  This exercises the incremental
// update stability that collision frame will rely on.

TEST(Adversarial3, TenFrameIncrementalUpdateCycle)
{
    std::vector<NodeHandle> ch;
    auto scene = make_scene(3, ch);

    set_world(scene.graph(), ch[0], translation(0.f, 0.f, 2.f));
    set_world(scene.graph(), ch[1], translation(0.f, 0.f, 5.f));
    set_world(scene.graph(), ch[2], translation(0.f, 0.f, 8.f));

    std::vector<RenderableDescriptor> descs(node_count(scene.graph()));
    descs[scene.root] = { classify_legacy_renderable(RenderPipeline::None) };
    for (int i = 0; i < 3; ++i)
        descs[ch[i]] = {
            classify_legacy_renderable(RenderPipeline::TransparentGeometry),
            static_cast<MeshHandle>(i), 0u, unit_box(), {}, true };

    auto view = wide_view_at(0.f);

    CompiledSceneStorage cs{};
    compile(cs, scene.graph(), descs, {}, view);

    RenderIRStorage ir{};
    build_render_ir(ir, cs.scene);

    RenderFrameStorage frame{};
    build_frame(frame, ir.ir, cs.scene);

    // 10 camera-move frames.
    for (int f = 1; f <= 10; ++f) {
        float cam_z = static_cast<float>(f) * 0.5f;
        auto new_view = wide_view_at(cam_z);

        update_view(cs, new_view);
        update_render_ir(ir, cs.scene);
        auto fv = update_frame_view(frame, ir.ir, cs.scene);

        ASSERT_EQ(fv.transparent.size(), 3u)
            << "frame " << f << ": all 3 transparent objects should survive";

        // Depths should all be distinct (objects at different z).
        float depths[3];
        for (int i = 0; i < 3; ++i)
            depths[i] = cs.scene.transparent[i].depth;

        EXPECT_NE(depths[0], depths[1])
            << "frame " << f;
        EXPECT_NE(depths[1], depths[2])
            << "frame " << f;
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// 11. Compile with only splats (no opaque, no transparent, no particles)
// ═════════════════════════════════════════════════════════════════════════════
//
// A scene with only splat renderables.  Other pipeline spans must be empty.
// Important for collision readiness: scenes may contain only non-mesh geometry.

TEST(Adversarial3, SplatOnlySceneEndToEnd)
{
    constexpr int N = 4;
    std::vector<NodeHandle> ch;
    auto scene = make_scene(N, ch);

    for (int i = 0; i < N; ++i)
        set_world(scene.graph(), ch[i],
                  translation(0.f, 0.f, static_cast<float>(i)));

    std::vector<RenderableDescriptor> descs(node_count(scene.graph()));
    descs[scene.root] = { classify_legacy_renderable(RenderPipeline::None) };
    for (int i = 0; i < N; ++i)
        descs[ch[i]] = {
            classify_legacy_renderable(RenderPipeline::Splat),
            INVALID_MESH, INVALID_MATERIAL, {},
            SplatDescriptor{}, true };

    auto view = wide_view_at(0.f);

    CompiledSceneStorage cs{};
    compile(cs, scene.graph(), descs, {}, view);

    EXPECT_EQ(cs.scene.opaque.size(), 0u);
    EXPECT_EQ(cs.scene.transparent.size(), 0u);
    EXPECT_EQ(cs.scene.particles.size(), 0u);
    EXPECT_EQ(cs.scene.splats.size(), static_cast<size_t>(N));

    RenderIRStorage ir{};
    auto ir_view = build_render_ir(ir, cs.scene);

    EXPECT_EQ(ir_view.opaque.size(), 0u);
    EXPECT_EQ(ir_view.transparent.size(), 0u);
    EXPECT_EQ(ir_view.splats.size(), static_cast<size_t>(N));

    RenderFrameStorage frame{};
    auto frame_view = build_frame(frame, ir_view, cs.scene);

    EXPECT_EQ(frame_view.opaque.size(), 0u);
    EXPECT_EQ(frame_view.splats.size(), static_cast<size_t>(N));
}


// ═════════════════════════════════════════════════════════════════════════════
// 12. update_compiled_transforms with empty dirty_nodes = all-dirty
// ═════════════════════════════════════════════════════════════════════════════
//
// The documented contract is that an empty dirty_nodes span means "treat all
// compiled outputs as dirty."  Verify this: move nodes in the graph and pass
// an empty dirty list.  ALL compiled records should be patched.
//
// This is critical for collision: the collision frame builder must understand
// that empty dirty_nodes is NOT a no-op — it's an all-dirty signal.

TEST(Adversarial3, EmptyDirtyListMeansAllDirty)
{
    std::vector<NodeHandle> ch;
    auto scene = make_scene(2, ch);

    set_world(scene.graph(), ch[0], translation(1.f, 0.f, 0.f));
    set_world(scene.graph(), ch[1], translation(2.f, 0.f, 0.f));

    std::vector<RenderableDescriptor> descs(node_count(scene.graph()));
    descs[scene.root] = { classify_legacy_renderable(RenderPipeline::None) };
    for (int i = 0; i < 2; ++i)
        descs[ch[i]] = {
            classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
            static_cast<MeshHandle>(i), 0u, unit_box(), {}, true };

    auto view = wide_view_at(0.f);
    CompiledSceneStorage storage{};
    compile(storage, scene.graph(), descs, {}, view);

    // Move nodes in the graph and pass empty dirty list (= all dirty).
    set_world(scene.graph(), ch[0], translation(999.f, 0.f, 0.f));
    set_world(scene.graph(), ch[1], translation(888.f, 0.f, 0.f));

    update_compiled_transforms(storage, scene.graph(), descs, view,
                               /*dirty_nodes=*/{}, false);

    // Both should have been patched — empty span = all dirty.
    // Find which opaque index corresponds to which node.
    uint32_t idx0 = storage.metadata.node_to_output[ch[0]].index;
    uint32_t idx1 = storage.metadata.node_to_output[ch[1]].index;

    EXPECT_FLOAT_EQ(storage.scene.opaque[idx0].world.m[12], 999.f)
        << "empty dirty list should patch all nodes (ch[0])";
    EXPECT_FLOAT_EQ(storage.scene.opaque[idx1].world.m[12], 888.f)
        << "empty dirty list should patch all nodes (ch[1])";
}


// ═════════════════════════════════════════════════════════════════════════════
// 13. Bounds transform through scale — compiled bounds must reflect world scale
// ═════════════════════════════════════════════════════════════════════════════
//
// A unit-box renderable at scale 10.  The compiled bounds should be 10x the
// local bounds.

TEST(Adversarial3, CompiledBoundsReflectWorldScale)
{
    std::vector<NodeHandle> ch;
    auto scene = make_scene(1, ch);

    set_world(scene.graph(), ch[0], scale_mat(10.0f));

    std::vector<RenderableDescriptor> descs(node_count(scene.graph()));
    descs[scene.root] = { classify_legacy_renderable(RenderPipeline::None) };
    descs[ch[0]] = {
        classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
        0u, 0u, unit_box(), {}, true };

    auto view = wide_view_at(0.f);
    CompiledSceneStorage cs{};
    compile(cs, scene.graph(), descs, {}, view);

    ASSERT_EQ(cs.scene.opaque.size(), 1u);

    const auto& bounds = cs.scene.opaque[0].bounds;
    // unit_box scaled by 10: [-5, 5] on each axis.
    EXPECT_NEAR(bounds.min.x, -5.0f, 0.01f);
    EXPECT_NEAR(bounds.max.x,  5.0f, 0.01f);
    EXPECT_NEAR(bounds.min.y, -5.0f, 0.01f);
    EXPECT_NEAR(bounds.max.y,  5.0f, 0.01f);
    EXPECT_NEAR(bounds.min.z, -5.0f, 0.01f);
    EXPECT_NEAR(bounds.max.z,  5.0f, 0.01f);
}


// ═════════════════════════════════════════════════════════════════════════════
// 14. Two dirty roots in same frame — both subtrees propagate
// ═════════════════════════════════════════════════════════════════════════════
//
// Two independent subtrees under root.  Dirty a node in each.  Both must
// propagate correctly.

TEST(Adversarial3, TwoDirtyRootsPropagateBothSubtrees)
{
    SceneBuilder b;

    TransformNode root_n{};
    NodeHandle root = add_node(b, root_n);

    // Subtree A: root → A0 → A1
    TransformNode a0_n{};
    a0_n.local = translation(1.f, 0.f, 0.f);
    NodeHandle a0 = add_node(b, a0_n);
    add_edge(b, root, a0);

    TransformNode a1_n{};
    a1_n.local = translation(1.f, 0.f, 0.f);
    NodeHandle a1 = add_node(b, a1_n);
    add_edge(b, a0, a1);

    // Subtree B: root → B0 → B1
    TransformNode b0_n{};
    b0_n.local = translation(0.f, 1.f, 0.f);
    NodeHandle b0 = add_node(b, b0_n);
    add_edge(b, root, b0);

    TransformNode b1_n{};
    b1_n.local = translation(0.f, 1.f, 0.f);
    NodeHandle b1 = add_node(b, b1_n);
    add_edge(b, b0, b1);

    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    auto& g = result->polytree;

    // Full propagation.
    const uint32_t frame1 = 1u;
    NodeHandle scratch[5];
    auto roots1 = collect_dirty_roots(g, frame1, scratch);
    update_static(g, roots1, frame1);

    EXPECT_FLOAT_EQ(node_data(g, a1).world.m[12], 2.f); // 1+1
    EXPECT_FLOAT_EQ(node_data(g, b1).world.m[13], 2.f); // 1+1

    // Dirty both subtree roots.
    const uint32_t frame2 = 2u;
    set_local(g, a0, translation(10.f, 0.f, 0.f));
    set_local(g, b0, translation(0.f, 10.f, 0.f));

    auto roots2 = collect_dirty_roots(g, frame2, scratch);
    EXPECT_EQ(roots2.size(), 2u)
        << "should have 2 dirty roots";

    update_static(g, roots2, frame2);

    EXPECT_FLOAT_EQ(node_data(g, a1).world.m[12], 11.f)
        << "subtree A leaf should be 10 + 1 = 11";
    EXPECT_FLOAT_EQ(node_data(g, b1).world.m[13], 11.f)
        << "subtree B leaf should be 10 + 1 = 11";
}


// ═════════════════════════════════════════════════════════════════════════════
// 15. Compiled scene recompile stability — double compile produces same result
// ═════════════════════════════════════════════════════════════════════════════
//
// Compiling the same scene twice into the same storage must produce
// identical output.  Tests that storage reuse doesn't leave stale data.

TEST(Adversarial3, DoubleCompileProducesIdenticalResult)
{
    constexpr int N = 5;
    std::vector<NodeHandle> ch;
    auto scene = make_scene(N, ch);

    for (int i = 0; i < N; ++i)
        set_world(scene.graph(), ch[i],
                  translation(static_cast<float>(i), 0.f, 0.f));

    std::vector<RenderableDescriptor> descs(node_count(scene.graph()));
    descs[scene.root] = { classify_legacy_renderable(RenderPipeline::None) };
    for (int i = 0; i < N; ++i)
        descs[ch[i]] = {
            classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
            static_cast<MeshHandle>(i),
            static_cast<MaterialHandle>(N - i),
            unit_box(), {}, true };

    auto view = wide_view_at(0.f);
    CompiledSceneStorage storage{};

    // First compile.
    compile(storage, scene.graph(), descs, {}, view);
    ASSERT_EQ(storage.scene.opaque.size(), static_cast<size_t>(N));

    // Record results.
    std::vector<float> x1(N), mesh1(N);
    for (int i = 0; i < N; ++i) {
        x1[i]    = storage.scene.opaque[i].world.m[12];
        mesh1[i] = static_cast<float>(storage.scene.opaque[i].mesh);
    }

    // Second compile — same inputs.
    compile(storage, scene.graph(), descs, {}, view);
    ASSERT_EQ(storage.scene.opaque.size(), static_cast<size_t>(N));

    for (int i = 0; i < N; ++i) {
        EXPECT_FLOAT_EQ(storage.scene.opaque[i].world.m[12], x1[i])
            << "double compile: world X mismatch at " << i;
        EXPECT_EQ(storage.scene.opaque[i].mesh, static_cast<MeshHandle>(mesh1[i]))
            << "double compile: mesh mismatch at " << i;
    }
}
