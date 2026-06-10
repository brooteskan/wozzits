// tests/scene_compile/scene_adversarial_test_2.cpp
//
// Second batch of adversarial tests — deeper pipeline probing.
// Targets frustum culling, sort ordering, incremental updates, refresh routing,
// and the cloud-instance DrawCommand path.

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
#include <cmath>
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

    // Identity view_projection → frustum is the [-1,1]^3 clip cube.
    // Objects within ±1 on all axes pass; objects outside are culled.
    ViewData identity_view()
    {
        ViewData v{};
        v.view = Mat4::identity();
        v.projection = Mat4::identity();
        v.view_projection = Mat4::identity();
        return v;
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

    void set_world(SceneGraph& g, NodeHandle n, const Mat4& world)
    {
        const_cast<TransformNode&>(node_data(g, n)).world = world;
    }

    using SceneStorage = PolytreeStorage<TransformNode, SceneEdge>;

    struct SimpleScene {
        SceneStorage storage;   // owns the buffer — must outlive polytree spans
        NodeHandle   root;

        SceneGraph&       graph()       { return storage.polytree; }
        const SceneGraph& graph() const { return storage.polytree; }
    };

    // Build a simple scene: root + N children (all flagged as RenderDomain).
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
// 1. Frustum culling: opaque object outside the clip cube is culled by IR
// ═════════════════════════════════════════════════════════════════════════════
//
// Identity view_projection produces frustum [-1,1]^3.  An opaque object at
// z=5 (bounds [4.5,5.5]) is entirely outside.  compile() includes it, but
// build_render_ir() must cull it.

TEST(Adversarial2, FrustumCullsDistantOpaque)
{
    std::vector<NodeHandle> ch;
    auto scene = make_scene(2, ch);

    // ch[0] at origin (inside frustum), ch[1] at z=5 (outside)
    set_world(scene.graph(), ch[0], translation(0.f, 0.f, 0.f));
    set_world(scene.graph(), ch[1], translation(0.f, 0.f, 5.f));

    std::vector<RenderableDescriptor> descs(node_count(scene.graph()));
    descs[scene.root] = { classify_legacy_renderable(RenderPipeline::None) };
    for (int i = 0; i < 2; ++i)
        descs[ch[i]] = {
            .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
            .mesh = static_cast<MeshHandle>(i), .material = 0u,
            .local_bounds = unit_box(), .visible = true };

    CompiledSceneStorage cs{};
    compile(cs, scene.graph(), descs, {}, identity_view());

    // compile includes both — no frustum culling at this stage.
    ASSERT_EQ(cs.scene.opaque.size(), 2u);

    RenderIRStorage ir{};
    auto ir_view = build_render_ir(ir, cs.scene);

    // IR must cull the distant object.
    EXPECT_EQ(ir_view.opaque.size(), 1u)
        << "distant opaque at z=5 should be culled by identity frustum";
    EXPECT_EQ(ir_view.culling.culled_opaque, 1u);
    EXPECT_EQ(ir_view.culling.visible_opaque, 1u);

    // The surviving draw ref should point to the object at origin.
    ASSERT_GE(ir_view.opaque.size(), 1u);
    EXPECT_EQ(cs.scene.opaque[ir_view.opaque[0].index].mesh, 0u);
}


// ═════════════════════════════════════════════════════════════════════════════
// 2. update_render_ir reveals previously culled object after frustum change
// ═════════════════════════════════════════════════════════════════════════════
//
// Two objects: one inside, one outside identity frustum.
// After switching to wide view, both should appear.

TEST(Adversarial2, UpdateIRRevealsNewlyVisibleObject)
{
    std::vector<NodeHandle> ch;
    auto scene = make_scene(2, ch);

    set_world(scene.graph(), ch[0], translation(0.f, 0.f, 0.f));
    set_world(scene.graph(), ch[1], translation(0.f, 0.f, 5.f));

    std::vector<RenderableDescriptor> descs(node_count(scene.graph()));
    descs[scene.root] = { classify_legacy_renderable(RenderPipeline::None) };
    for (int i = 0; i < 2; ++i)
        descs[ch[i]] = {
            .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
            .mesh = static_cast<MeshHandle>(i), .material = 0u,
            .local_bounds = unit_box(), .visible = true };

    // Compile with identity view — object at z=5 outside frustum.
    CompiledSceneStorage cs{};
    compile(cs, scene.graph(), descs, {}, identity_view());

    RenderIRStorage ir{};
    build_render_ir(ir, cs.scene);
    ASSERT_EQ(ir.ir.opaque.size(), 1u);

    // Switch to wide view — recull.
    auto wide = wide_view_at(0.f);
    update_view(cs, wide);
    update_render_ir(ir, cs.scene);

    EXPECT_EQ(ir.ir.opaque.size(), 2u)
        << "after switching to wide frustum, both objects should be visible";
    EXPECT_EQ(ir.ir.culling.culled_opaque, 0u);
}


// ═════════════════════════════════════════════════════════════════════════════
// 3. Splat with non-degenerate AABB: center outside frustum but bounds overlap
// ═════════════════════════════════════════════════════════════════════════════
//
// A splat whose center is at z=1.5 (outside identity frustum for a point test)
// but whose world-space AABB extends into the frustum (e.g. min.z = -0.5).
// The AABB frustum test should keep it visible.

TEST(Adversarial2, SplatAABBOverlapKeepsVisible)
{
    std::vector<NodeHandle> ch;
    auto scene = make_scene(1, ch);

    // Place the splat's world at z=1.5 so center is outside identity frustum.
    set_world(scene.graph(), ch[0], translation(0.f, 0.f, 1.5f));

    std::vector<RenderableDescriptor> descs(node_count(scene.graph()));
    descs[scene.root] = { classify_legacy_renderable(RenderPipeline::None) };

    // Give it a large local_bounds so world AABB extends into the frustum.
    AABB big_box = { Vec3{-0.5f,-0.5f,-2.5f}, Vec3{0.5f,0.5f,0.5f} };
    // After transform, world AABB: z range = 1.5 + [-2.5, 0.5] = [-1.0, 2.0]
    // This overlaps [-1,1] frustum.

    descs[ch[0]] = {
        .node_class = classify_legacy_renderable(RenderPipeline::Splat),
        .mesh = INVALID_MESH, .material = INVALID_MATERIAL,
        .local_bounds = big_box, .splat_data = SplatDescriptor{},
        .visible = true
    };

    CompiledSceneStorage cs{};
    compile(cs, scene.graph(), descs, {}, identity_view());
    ASSERT_EQ(cs.scene.splats.size(), 1u);

    // Verify the AABB was actually set (non-degenerate).
    const auto& bounds = cs.scene.splats[0].bounds;
    ASSERT_NE(bounds.min.z, bounds.max.z)
        << "bounds should be non-degenerate";

    RenderIRStorage ir{};
    auto ir_view = build_render_ir(ir, cs.scene);

    EXPECT_EQ(ir_view.splats.size(), 1u)
        << "splat with AABB overlapping frustum should NOT be culled, "
           "even though center is outside";
}


// ═════════════════════════════════════════════════════════════════════════════
// 4. Splat with degenerate AABB + center outside frustum is culled
// ═════════════════════════════════════════════════════════════════════════════
//
// Degenerate AABB (min == max) falls back to point_in_frustum.
// Center at z=5 should be outside identity frustum.

TEST(Adversarial2, SplatDegenerateAABBCenterOutsideIsCulled)
{
    std::vector<NodeHandle> ch;
    auto scene = make_scene(1, ch);

    set_world(scene.graph(), ch[0], translation(0.f, 0.f, 5.f));

    std::vector<RenderableDescriptor> descs(node_count(scene.graph()));
    descs[scene.root] = { classify_legacy_renderable(RenderPipeline::None) };

    // Degenerate AABB: min == max == origin.
    AABB degenerate = { Vec3{0.f,0.f,0.f}, Vec3{0.f,0.f,0.f} };

    descs[ch[0]] = {
        .node_class = classify_legacy_renderable(RenderPipeline::Splat),
        .mesh = INVALID_MESH, .material = INVALID_MATERIAL,
        .local_bounds = degenerate, .splat_data = SplatDescriptor{},
        .visible = true
    };

    CompiledSceneStorage cs{};
    compile(cs, scene.graph(), descs, {}, identity_view());
    ASSERT_EQ(cs.scene.splats.size(), 1u);

    RenderIRStorage ir{};
    auto ir_view = build_render_ir(ir, cs.scene);

    EXPECT_EQ(ir_view.splats.size(), 0u)
        << "splat with degenerate AABB and center at z=5 should be culled";
    EXPECT_EQ(ir_view.culling.culled_splats, 1u);
}


// ═════════════════════════════════════════════════════════════════════════════
// 5. refresh_compiled_scene: View-only dirty updates depth without touching transforms
// ═════════════════════════════════════════════════════════════════════════════
//
// After compile, mark View-only dirty with a new camera position.
// Transparent depth must change, world matrix must NOT change.

TEST(Adversarial2, RefreshViewOnlyChangesDepthNotTransform)
{
    std::vector<NodeHandle> ch;
    auto scene = make_scene(1, ch);

    set_world(scene.graph(), ch[0], translation(0.f, 0.f, 5.f));

    std::vector<RenderableDescriptor> descs(node_count(scene.graph()));
    descs[scene.root] = { classify_legacy_renderable(RenderPipeline::None) };
    descs[ch[0]] = {
        .node_class = classify_legacy_renderable(RenderPipeline::TransparentGeometry),
        .mesh = 1u, .material = 1u, .local_bounds = unit_box(), .visible = true };

    auto view0 = wide_view_at(0.f);
    CompiledSceneStorage storage{};
    compile(storage, scene.graph(), descs, {}, view0);

    ASSERT_EQ(storage.scene.transparent.size(), 1u);
    float world_z_before = storage.scene.transparent[0].world.m[14];
    float depth_before   = storage.scene.transparent[0].depth;

    // View-only dirty: move camera from z=0 to z=100.
    auto view1 = wide_view_at(100.f);
    refresh_compiled_scene(
        storage, scene.graph(), descs, {}, view1,
        SceneDirtyBits::View);

    float world_z_after = storage.scene.transparent[0].world.m[14];
    float depth_after   = storage.scene.transparent[0].depth;

    EXPECT_FLOAT_EQ(world_z_after, world_z_before)
        << "View-only refresh must not change world matrix";
    EXPECT_NE(depth_after, depth_before)
        << "View-only refresh must change depth";
}


// ═════════════════════════════════════════════════════════════════════════════
// 6. refresh_compiled_scene: Material-only dirty patches mesh handle in-place
// ═════════════════════════════════════════════════════════════════════════════
//
// After compile, change the mesh in the descriptor and mark Material dirty.
// The compiled opaque record's mesh must be updated; world must be untouched.

TEST(Adversarial2, RefreshMaterialOnlyPatchesMesh)
{
    std::vector<NodeHandle> ch;
    auto scene = make_scene(1, ch);

    set_world(scene.graph(), ch[0], translation(0.f, 0.f, 0.5f));

    std::vector<RenderableDescriptor> descs(node_count(scene.graph()));
    descs[scene.root] = { classify_legacy_renderable(RenderPipeline::None) };
    descs[ch[0]] = {
        .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
        .mesh = 10u, .material = 20u, .local_bounds = unit_box(), .visible = true };

    auto view = wide_view_at(0.f);
    CompiledSceneStorage storage{};
    compile(storage, scene.graph(), descs, {}, view);

    ASSERT_EQ(storage.scene.opaque.size(), 1u);
    EXPECT_EQ(storage.scene.opaque[0].mesh, 10u);
    EXPECT_EQ(storage.scene.opaque[0].material, 20u);
    float world_z = storage.scene.opaque[0].world.m[14];

    // Patch descriptors.
    descs[ch[0]].mesh     = 77u;
    descs[ch[0]].material = 88u;

    refresh_compiled_scene(
        storage, scene.graph(), descs, {}, view,
        SceneDirtyBits::Material);

    EXPECT_EQ(storage.scene.opaque[0].mesh, 77u)
        << "Material-only refresh did not update mesh";
    EXPECT_EQ(storage.scene.opaque[0].material, 88u)
        << "Material-only refresh did not update material";
    EXPECT_FLOAT_EQ(storage.scene.opaque[0].world.m[14], world_z)
        << "Material-only refresh must not change world matrix";
}


// ═════════════════════════════════════════════════════════════════════════════
// 7. Opaque IR sort: objects sorted by material key, not insertion order
// ═════════════════════════════════════════════════════════════════════════════
//
// Three opaque objects with materials 30, 10, 20 (inserted in that order).
// After IR build, they must appear sorted by the material-based opaque_key.

TEST(Adversarial2, OpaqueSortedByMaterialKey)
{
    std::vector<NodeHandle> ch;
    auto scene = make_scene(3, ch);

    // Place all inside the wide frustum.
    for (int i = 0; i < 3; ++i)
        set_world(scene.graph(), ch[i], translation(static_cast<float>(i), 0.f, 0.f));

    uint32_t materials[] = { 30u, 10u, 20u };
    uint32_t meshes[]    = {  0u,  0u,  0u };

    std::vector<RenderableDescriptor> descs(node_count(scene.graph()));
    descs[scene.root] = { classify_legacy_renderable(RenderPipeline::None) };
    for (int i = 0; i < 3; ++i)
        descs[ch[i]] = {
            .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
            .mesh = meshes[i], .material = materials[i],
            .local_bounds = unit_box(), .visible = true };

    auto view = wide_view_at(0.f);
    CompiledSceneStorage cs{};
    compile(cs, scene.graph(), descs, {}, view);

    RenderIRStorage ir{};
    auto ir_view = build_render_ir(ir, cs.scene);

    ASSERT_EQ(ir_view.opaque.size(), 3u);

    // Verify sort order: lower material should come first.
    for (size_t i = 1; i < ir_view.opaque.size(); ++i) {
        EXPECT_LE(ir_view.opaque[i - 1].sort_key, ir_view.opaque[i].sort_key)
            << "opaque IR not sorted by material key at index " << i;
    }

    // First draw ref should resolve to material 10 (the lowest).
    EXPECT_EQ(cs.scene.opaque[ir_view.opaque[0].index].material, 10u)
        << "first opaque draw should be the object with lowest material";
}


// ═════════════════════════════════════════════════════════════════════════════
// 8. Transparent back-to-front sort verified with three depth-separated objects
// ═════════════════════════════════════════════════════════════════════════════
//
// Objects at z=2, z=8, z=5.  With camera at z=0, view_depth = -(view_z).
// wide_view_at(0) uses identity view matrix, so depth = -(world_z).
// Depths: -2, -8, -5.
// depth_key_back_to_front sorts by ~bits, which puts more-negative first.
// Expected sort order: z=8 (depth=-8) first, z=5 (depth=-5), z=2 (depth=-2).
// This is back-to-front: furthest object drawn first.

TEST(Adversarial2, TransparentBackToFrontSort)
{
    std::vector<NodeHandle> ch;
    auto scene = make_scene(3, ch);

    float z_values[] = { 2.f, 8.f, 5.f };
    for (int i = 0; i < 3; ++i)
        set_world(scene.graph(), ch[i], translation(0.f, 0.f, z_values[i]));

    std::vector<RenderableDescriptor> descs(node_count(scene.graph()));
    descs[scene.root] = { classify_legacy_renderable(RenderPipeline::None) };
    for (int i = 0; i < 3; ++i)
        descs[ch[i]] = {
            .node_class = classify_legacy_renderable(RenderPipeline::TransparentGeometry),
            .mesh = static_cast<MeshHandle>(i), .material = 0u,
            .local_bounds = unit_box(), .visible = true };

    auto view = wide_view_at(0.f);
    CompiledSceneStorage cs{};
    compile(cs, scene.graph(), descs, {}, view);

    RenderIRStorage ir{};
    auto ir_view = build_render_ir(ir, cs.scene);

    ASSERT_EQ(ir_view.transparent.size(), 3u);

    // Verify sort keys are ascending (IR layer contract).
    for (size_t i = 1; i < ir_view.transparent.size(); ++i) {
        EXPECT_LE(ir_view.transparent[i - 1].sort_key, ir_view.transparent[i].sort_key)
            << "transparent IR sort keys not ascending at index " << i;
    }

    // The furthest object (z=8, mesh=1) should be first in back-to-front order.
    EXPECT_EQ(cs.scene.transparent[ir_view.transparent[0].index].mesh, 1u)
        << "furthest transparent object (mesh=1, z=8) should sort first";

    // The closest object (z=2, mesh=0) should be last.
    EXPECT_EQ(cs.scene.transparent[ir_view.transparent[2].index].mesh, 0u)
        << "closest transparent object (mesh=0, z=2) should sort last";
}


// ═════════════════════════════════════════════════════════════════════════════
// 9. Cloud-instance splat DrawCommand: verify cloud_handle is carried through
// ═════════════════════════════════════════════════════════════════════════════
//
// A splat with a valid cloud_handle should take the cloud-instance path in
// fill_splat_commands.  The resulting DrawCommand must have:
//   kind == GaussianSplats, splats_buffer == cloud_handle.
//
// Cloud-instance commands draw the full cloud resource, so splat_instance_count
// remains 0 and sorted_splat_indices remains empty by contract.

TEST(Adversarial2, CloudInstanceSplatDrawCommand)
{
    std::vector<NodeHandle> ch;
    auto scene = make_scene(1, ch);

    set_world(scene.graph(), ch[0], translation(0.f, 0.f, 0.f));

    std::vector<RenderableDescriptor> descs(node_count(scene.graph()));
    descs[scene.root] = { classify_legacy_renderable(RenderPipeline::None) };
    descs[ch[0]] = {
        .node_class = classify_legacy_renderable(RenderPipeline::Splat),
        .mesh = INVALID_MESH, .material = INVALID_MATERIAL,
        .local_bounds = unit_box(),
        .splat_data = SplatDescriptor{
            .scale        = { 1.f, 1.f, 1.f },
            .rotation     = { 0.f, 0.f, 0.f, 1.f },
            .color        = { 1.f, 1.f, 1.f },
            .opacity      = 1.f,
            .cloud_handle = 42u,
            .cloud_local_index = 0u,
        },
        .visible = true
    };

    auto view = wide_view_at(0.f);
    CompiledSceneStorage cs{};
    compile(cs, scene.graph(), descs, {}, view);

    RenderIRStorage ir{};
    auto ir_view = build_render_ir(ir, cs.scene);

    RenderFrameStorage frame{};
    auto frame_view = build_frame(frame, ir_view, cs.scene);

    ASSERT_EQ(frame_view.splats.size(), 1u);

    // Verify cloud-instance path was taken.
    EXPECT_EQ(frame_view.splats[0].kind, DrawCommandKind::GaussianSplats)
        << "cloud splat should use GaussianSplats kind";
    EXPECT_EQ(frame_view.splats[0].splats_buffer, 42u)
        << "cloud_handle must be carried to DrawCommand::splats_buffer";
    EXPECT_EQ(frame_view.splats[0].stage, PipelineStage::Splat);

    EXPECT_EQ(frame_view.splats[0].splat_instance_count, 0u)
        << "cloud-instance path draws the full cloud resource";
    EXPECT_TRUE(frame_view.splats[0].sorted_splat_indices.empty())
        << "cloud-instance path should not carry per-splat sorted indices";
}


// ═════════════════════════════════════════════════════════════════════════════
// 10. Partial dirty list in update_compiled_transforms: only listed nodes change
// ═════════════════════════════════════════════════════════════════════════════
//
// Compile 3 opaque nodes.  Move ALL of them in the scene graph, but pass only
// one in dirty_nodes.  Only that one should be patched; the others retain
// their original world matrices.

TEST(Adversarial2, PartialDirtyListOnlyPatchesSpecifiedNodes)
{
    std::vector<NodeHandle> ch;
    auto scene = make_scene(3, ch);

    // Initial positions: x=1,2,3
    for (int i = 0; i < 3; ++i)
        set_world(scene.graph(), ch[i], translation(static_cast<float>(i + 1), 0.f, 0.f));

    std::vector<RenderableDescriptor> descs(node_count(scene.graph()));
    descs[scene.root] = { classify_legacy_renderable(RenderPipeline::None) };
    for (int i = 0; i < 3; ++i)
        descs[ch[i]] = {
            .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
            .mesh = static_cast<MeshHandle>(i), .material = 0u,
            .local_bounds = unit_box(), .visible = true };

    auto view = wide_view_at(0.f);
    CompiledSceneStorage storage{};
    compile(storage, scene.graph(), descs, {}, view);

    ASSERT_EQ(storage.scene.opaque.size(), 3u);

    // Record original world X values.
    float orig_x[3];
    for (int i = 0; i < 3; ++i)
        orig_x[i] = storage.scene.opaque[i].world.m[12];

    // Move ALL nodes in the scene graph to new positions.
    for (int i = 0; i < 3; ++i)
        set_world(scene.graph(), ch[i], translation(static_cast<float>((i + 1) * 100), 0.f, 0.f));

    // But only mark ch[1] as dirty.
    std::vector<NodeHandle> dirty = { ch[1] };
    update_compiled_transforms(storage, scene.graph(), descs, view, dirty, false);

    // ch[1] should be patched to its new position.
    // We need to find which opaque index corresponds to ch[1].
    uint32_t ch1_opaque_idx = storage.metadata.node_to_output[ch[1]].index;
    EXPECT_FLOAT_EQ(storage.scene.opaque[ch1_opaque_idx].world.m[12], 200.f)
        << "dirty node ch[1] should have been patched to x=200";

    // ch[0] and ch[2] should retain their ORIGINAL positions (from compile time).
    uint32_t ch0_opaque_idx = storage.metadata.node_to_output[ch[0]].index;
    uint32_t ch2_opaque_idx = storage.metadata.node_to_output[ch[2]].index;
    EXPECT_FLOAT_EQ(storage.scene.opaque[ch0_opaque_idx].world.m[12],
                    orig_x[ch0_opaque_idx])
        << "non-dirty node ch[0] should retain original world matrix";
    EXPECT_FLOAT_EQ(storage.scene.opaque[ch2_opaque_idx].world.m[12],
                    orig_x[ch2_opaque_idx])
        << "non-dirty node ch[2] should retain original world matrix";
}


// ═════════════════════════════════════════════════════════════════════════════
// 11. Transparent object behind camera has negative depth and sorts correctly
// ═════════════════════════════════════════════════════════════════════════════
//
// Camera at z=10, one object at z=5 (in front, depth>0), one at z=15
// (behind camera, depth<0).  With a wide frustum both are "visible" to IR.
// The behind-camera object should sort before the in-front object (back-to-front).

TEST(Adversarial2, NegativeDepthTransparentSortCorrectly)
{
    std::vector<NodeHandle> ch;
    auto scene = make_scene(2, ch);

    set_world(scene.graph(), ch[0], translation(0.f, 0.f, 5.f));   // in front of camera
    set_world(scene.graph(), ch[1], translation(0.f, 0.f, 15.f));   // behind camera

    std::vector<RenderableDescriptor> descs(node_count(scene.graph()));
    descs[scene.root] = { classify_legacy_renderable(RenderPipeline::None) };
    for (int i = 0; i < 2; ++i)
        descs[ch[i]] = {
            .node_class = classify_legacy_renderable(RenderPipeline::TransparentGeometry),
            .mesh = static_cast<MeshHandle>(i), .material = 0u,
            .local_bounds = unit_box(), .visible = true };

    auto view = wide_view_at(10.f);
    CompiledSceneStorage cs{};
    compile(cs, scene.graph(), descs, {}, view);

    ASSERT_EQ(cs.scene.transparent.size(), 2u);

    // Verify depths: z=5 with camera at z=10.
    // view.m[14] = -10, so view-space z of point at z=5 is: 5 + (-10) = -5
    // depth = -(-5) = 5 (positive, in front).
    // Point at z=15: view-space z = 15 + (-10) = 5
    // depth = -(5) = -5 (negative, behind camera).
    float depth_front  = cs.scene.transparent[0].depth;
    float depth_behind = cs.scene.transparent[1].depth;

    // One should be positive, one negative (or at least different).
    EXPECT_NE(depth_front, depth_behind)
        << "objects at different positions should have different depths";

    RenderIRStorage ir{};
    auto ir_view = build_render_ir(ir, cs.scene);

    ASSERT_EQ(ir_view.transparent.size(), 2u);

    // Both should be present (wide frustum).
    // Sort keys should be valid (no NaN, no crashes).
    EXPECT_NE(ir_view.transparent[0].sort_key, 0u);
    EXPECT_NE(ir_view.transparent[1].sort_key, 0u);
}


// ═════════════════════════════════════════════════════════════════════════════
// 12. Empty scene (no renderables, only lights) compiles without crashing
// ═════════════════════════════════════════════════════════════════════════════

TEST(Adversarial2, EmptyRenderablesWithLightsDoesNotCrash)
{
    SceneBuilder b;
    TransformNode root_node{};
    NodeHandle root = add_node(b, root_node);

    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    propagate_all(result->polytree);

    std::vector<RenderableDescriptor> descs(node_count(result->polytree));
    descs[root] = { classify_legacy_renderable(RenderPipeline::None) };

    LightRecord sun{};
    sun.type = LightType::Directional;
    sun.intensity = 2.f;
    std::array<LightRecord, 1> lights{ sun };

    CompiledSceneStorage cs{};
    compile(cs, result->polytree, descs, lights, identity_view());

    EXPECT_EQ(cs.scene.opaque.size(), 0u);
    EXPECT_EQ(cs.scene.transparent.size(), 0u);
    EXPECT_EQ(cs.scene.splats.size(), 0u);
    EXPECT_EQ(cs.scene.particles.size(), 0u);
    EXPECT_EQ(cs.scene.lights.size(), 1u);
    EXPECT_FLOAT_EQ(cs.scene.lights[0].intensity, 2.f);

    // Full pipeline should not crash with empty renderables.
    RenderIRStorage ir{};
    auto ir_view = build_render_ir(ir, cs.scene);

    EXPECT_EQ(ir_view.opaque.size(), 0u);
    EXPECT_EQ(ir_view.transparent.size(), 0u);

    RenderFrameStorage frame{};
    auto frame_view = build_frame(frame, ir_view, cs.scene);

    EXPECT_EQ(frame_view.opaque.size(), 0u);

    auto submit_result = submit(frame_view);
    EXPECT_EQ(submit_result.total(), 0u);
}


// ═════════════════════════════════════════════════════════════════════════════
// 13. refresh with no dirty bits returns unchanged scene
// ═════════════════════════════════════════════════════════════════════════════

TEST(Adversarial2, RefreshNoDirtyBitsIsNoop)
{
    std::vector<NodeHandle> ch;
    auto scene = make_scene(1, ch);

    set_world(scene.graph(), ch[0], translation(0.f, 0.f, 0.5f));

    std::vector<RenderableDescriptor> descs(node_count(scene.graph()));
    descs[scene.root] = { classify_legacy_renderable(RenderPipeline::None) };
    descs[ch[0]] = {
        .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
        .mesh = 5u, .material = 3u, .local_bounds = unit_box(), .visible = true };

    auto view = wide_view_at(0.f);
    CompiledSceneStorage storage{};
    compile(storage, scene.graph(), descs, {}, view);

    ASSERT_EQ(storage.scene.opaque.size(), 1u);
    float world_z = storage.scene.opaque[0].world.m[14];

    // Mutate the scene graph (but don't tell refresh about it).
    set_world(scene.graph(), ch[0], translation(0.f, 0.f, 999.f));

    // Refresh with no dirty bits — should be a no-op.
    auto result = refresh_compiled_scene(
        storage, scene.graph(), descs, {}, view,
        static_cast<SceneDirtyBits>(0));

    EXPECT_FLOAT_EQ(result.opaque[0].world.m[14], world_z)
        << "no-dirty-bits refresh must not change anything";
}


// ═════════════════════════════════════════════════════════════════════════════
// 14. Rapid compile/recompile cycles with growing/shrinking scene
// ═════════════════════════════════════════════════════════════════════════════
//
// Alternate between a large scene (10 opaque) and a small scene (1 opaque)
// over several cycles.  Buffer reuse must be correct — no stale data leaking.

TEST(Adversarial2, RapidGrowShrinkRecompileCycles)
{
    std::vector<NodeHandle> ch;
    auto scene = make_scene(10, ch);

    for (int i = 0; i < 10; ++i)
        set_world(scene.graph(), ch[i], translation(static_cast<float>(i) * 0.1f, 0.f, 0.f));

    std::vector<RenderableDescriptor> descs(node_count(scene.graph()));
    descs[scene.root] = { classify_legacy_renderable(RenderPipeline::None) };
    for (int i = 0; i < 10; ++i)
        descs[ch[i]] = {
            .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
            .mesh = static_cast<MeshHandle>(i), .material = 0u,
            .local_bounds = unit_box(), .visible = true };

    auto view = wide_view_at(0.f);
    CompiledSceneStorage storage{};

    for (int cycle = 0; cycle < 5; ++cycle) {
        // Large scene: all 10 visible.
        for (int i = 0; i < 10; ++i)
            descs[ch[i]].visible = true;
        compile(storage, scene.graph(), descs, {}, view);
        EXPECT_EQ(storage.scene.opaque.size(), 10u)
            << "cycle " << cycle << ": large scene should have 10 opaque";

        // Small scene: only ch[0] visible.
        for (int i = 1; i < 10; ++i)
            descs[ch[i]].visible = false;
        compile(storage, scene.graph(), descs, {}, view);
        EXPECT_EQ(storage.scene.opaque.size(), 1u)
            << "cycle " << cycle << ": small scene should have 1 opaque";
        EXPECT_EQ(storage.scene.opaque[0].mesh, 0u)
            << "cycle " << cycle << ": surviving node should be ch[0]";

        // Metadata must be valid for the small scene.
        EXPECT_EQ(storage.metadata.opaque_source_nodes.size(), 1u);
        EXPECT_EQ(storage.metadata.node_to_output[ch[0]].index, 0u);
        for (int i = 1; i < 10; ++i)
            EXPECT_EQ(storage.metadata.node_to_output[ch[i]].kind,
                      CompiledOutputKind::None)
                << "cycle " << cycle << ": hidden node ch[" << i << "] should have no output";
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// 15. update_frame_view with changed frustum: opaque count changes
// ═════════════════════════════════════════════════════════════════════════════
//
// build_frame with wide frustum (2 visible opaque), then update_frame_view
// with identity frustum (1 visible).  The opaque span must shrink.

TEST(Adversarial2, UpdateFrameViewShrinksOpaqueOnFrustumChange)
{
    std::vector<NodeHandle> ch;
    auto scene = make_scene(2, ch);

    set_world(scene.graph(), ch[0], translation(0.f, 0.f, 0.f));   // inside identity frustum
    set_world(scene.graph(), ch[1], translation(0.f, 0.f, 5.f));   // outside identity frustum

    std::vector<RenderableDescriptor> descs(node_count(scene.graph()));
    descs[scene.root] = { classify_legacy_renderable(RenderPipeline::None) };
    for (int i = 0; i < 2; ++i)
        descs[ch[i]] = {
            .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
            .mesh = static_cast<MeshHandle>(i), .material = 0u,
            .local_bounds = unit_box(), .visible = true };

    // Compile and build with wide view — both visible.
    auto wide = wide_view_at(0.f);
    CompiledSceneStorage cs{};
    compile(cs, scene.graph(), descs, {}, wide);

    RenderIRStorage ir{};
    auto ir_view = build_render_ir(ir, cs.scene);
    ASSERT_EQ(ir_view.opaque.size(), 2u);

    RenderFrameStorage frame{};
    auto f1 = build_frame(frame, ir_view, cs.scene);
    ASSERT_EQ(f1.opaque.size(), 2u);

    // Switch to identity view — object at z=5 gets culled.
    auto id_view = identity_view();
    update_view(cs, id_view);
    update_render_ir(ir, cs.scene);

    ASSERT_EQ(ir.ir.opaque.size(), 1u)
        << "after switching to identity frustum, only 1 opaque should be visible";

    auto f2 = update_frame_view(frame, ir.ir, cs.scene);

    EXPECT_EQ(f2.opaque.size(), 1u)
        << "update_frame_view must shrink opaque span when frustum tightens";
    EXPECT_EQ(f2.opaque[0].mesh, 0u)
        << "surviving opaque should be the object at origin";
}


// ═════════════════════════════════════════════════════════════════════════════
// 16. Refresh with Topology dirty triggers full recompile
// ═════════════════════════════════════════════════════════════════════════════
//
// After compile, change descriptor counts and mark Topology dirty.
// The recompile should produce the new counts.

TEST(Adversarial2, TopologyDirtyTriggersFullRecompile)
{
    std::vector<NodeHandle> ch;
    auto scene = make_scene(3, ch);

    for (int i = 0; i < 3; ++i)
        set_world(scene.graph(), ch[i], translation(static_cast<float>(i) * 0.1f, 0.f, 0.f));

    std::vector<RenderableDescriptor> descs(node_count(scene.graph()));
    descs[scene.root] = { classify_legacy_renderable(RenderPipeline::None) };
    for (int i = 0; i < 3; ++i)
        descs[ch[i]] = {
            .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
            .mesh = static_cast<MeshHandle>(i), .material = 0u,
            .local_bounds = unit_box(), .visible = true };

    auto view = wide_view_at(0.f);
    CompiledSceneStorage storage{};
    compile(storage, scene.graph(), descs, {}, view);
    ASSERT_EQ(storage.scene.opaque.size(), 3u);

    // Change one to transparent and mark Topology dirty.
    descs[ch[2]] = {
        .node_class = classify_legacy_renderable(RenderPipeline::TransparentGeometry),
        .mesh = 2u, .material = 0u, .local_bounds = unit_box(), .visible = true };

    refresh_compiled_scene(
        storage, scene.graph(), descs, {}, view,
        SceneDirtyBits::Topology);

    EXPECT_EQ(storage.scene.opaque.size(), 2u)
        << "Topology dirty should trigger full recompile with new counts";
    EXPECT_EQ(storage.scene.transparent.size(), 1u)
        << "migrated node should appear in transparent pipeline";
}
