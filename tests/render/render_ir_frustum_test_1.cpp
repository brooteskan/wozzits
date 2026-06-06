#include <gtest/gtest.h>
#include <render/ir/render_ir.h>
#include <scene/compile/scene_compiler.h>
#include <scene/compile/legacy_classification.h>

using namespace wz::render;
using namespace wz::scene;
using namespace wz::core::graph;
using namespace wz::math;

// ─── Helpers ──────────────────────────────────────────────────────────────────

namespace {

    Mat4 translation_z(float z)
    {
        Mat4 m = Mat4::identity();
        m.m[14] = z;
        return m;
    }

    AABB unit_box()
    {
        return { Vec3{-0.5f,-0.5f,-0.5f}, Vec3{0.5f,0.5f,0.5f} };
    }

    // Unit-cube frustum: [-1, 1]^3 in world space.
    // Objects at z=0 with unit_box are inside; objects at z=10 are outside.
    ViewData unit_frustum_view()
    {
        ViewData v{};
        v.view            = Mat4::identity();
        v.projection      = Mat4::identity();
        v.view_projection = Mat4::identity();
        return v;
    }

    // Frustum shifted to [z-1, z+1] in world z, [-1, 1] in x and y.
    // VP = identity with m[14] = -z shifts the z clipping planes by z.
    ViewData frustum_at_z(float z)
    {
        ViewData v{};
        v.view = Mat4::identity();
        v.view.m[14] = -z; // for depth computation
        v.projection = Mat4::identity();
        v.view_projection = Mat4::identity();
        v.view_projection.m[14] = -z; // shift frustum: near = z-1, far = z+1
        v.camera_position = Vec3{ 0.f, 0.f, z };
        return v;
    }

    // Build a scene with one opaque primitive at the given z position.
    CompiledSceneStorage make_one_opaque(float z,
                                         AABB bounds,
                                         ViewData view)
    {
        SceneBuilder b;
        TransformNode root{}; root.local = Mat4::identity();
        NodeHandle root_h = add_node(b, root);
        TransformNode obj{}; obj.local = translation_z(z);
        obj.flags = TransformNodeFlag::RenderDomain;
        NodeHandle obj_h = add_node(b, obj);
        add_edge(b, root_h, obj_h);

        auto s = build(std::move(b));
        assert(s.has_value());
        propagate_all(s->polytree);

        std::vector<RenderableDescriptor> descs(node_count(s->polytree));
        descs[root_h] = { classify_legacy_renderable(RenderPipeline::None) };
        descs[obj_h]  = {
            classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
            1u, 1u, bounds
        };

        CompiledSceneStorage cs{};
        compile(cs, s->polytree, descs, {}, view);
        return cs;
    }

    // Build a scene with two opaque primitives: A at z_a, B at z_b.
    struct TwoOpaqueScene {
        CompiledSceneStorage compiled;
        NodeHandle           node_a{};
        NodeHandle           node_b{};
    };

    TwoOpaqueScene make_two_opaque(float z_a, float z_b, ViewData view)
    {
        SceneBuilder b;
        TransformNode root{}; root.local = Mat4::identity();
        NodeHandle root_h = add_node(b, root);

        TransformNode obj_a{}; obj_a.local = translation_z(z_a);
        obj_a.flags = TransformNodeFlag::RenderDomain;
        NodeHandle na = add_node(b, obj_a);
        add_edge(b, root_h, na);

        TransformNode obj_b{}; obj_b.local = translation_z(z_b);
        obj_b.flags = TransformNodeFlag::RenderDomain;
        NodeHandle nb = add_node(b, obj_b);
        add_edge(b, root_h, nb);

        auto s = build(std::move(b));
        assert(s.has_value());
        propagate_all(s->polytree);

        std::vector<RenderableDescriptor> descs(node_count(s->polytree));
        descs[root_h] = { classify_legacy_renderable(RenderPipeline::None) };
        descs[na]     = { classify_legacy_renderable(RenderPipeline::OpaqueGeometry), 1u, 1u, unit_box() };
        descs[nb]     = { classify_legacy_renderable(RenderPipeline::OpaqueGeometry), 2u, 2u, unit_box() };

        TwoOpaqueScene out{};
        compile(out.compiled, s->polytree, descs, {}, view);
        out.node_a = na;
        out.node_b = nb;
        return out;
    }

} // namespace


// ─── AABB inside frustum ──────────────────────────────────────────────────────

TEST(RenderIRFrustum, AABBInsideFrustumIsEmitted)
{
    // unit_box at z=0: world bounds [-0.5, 0.5]^3 — fully inside unit-cube frustum.
    auto cs = make_one_opaque(0.f, unit_box(), unit_frustum_view());
    RenderIRStorage ir_st;
    auto ir = build_render_ir(ir_st, cs.scene);

    EXPECT_EQ(ir.opaque.size(), 1u) << "inside AABB was culled";
    EXPECT_EQ(ir.culling.visible_opaque, 1u);
    EXPECT_EQ(ir.culling.culled_opaque,  0u);
}


// ─── AABB outside frustum ─────────────────────────────────────────────────────

TEST(RenderIRFrustum, AABBOutsideFrustumIsCulled)
{
    // unit_box at z=10: world bounds [9.5, 10.5] in z — outside unit-cube frustum (far at z=1).
    auto cs = make_one_opaque(10.f, unit_box(), unit_frustum_view());
    RenderIRStorage ir_st;
    auto ir = build_render_ir(ir_st, cs.scene);

    EXPECT_EQ(ir.opaque.size(), 0u) << "outside AABB was not culled";
    EXPECT_EQ(ir.culling.visible_opaque, 0u);
    EXPECT_EQ(ir.culling.culled_opaque,  1u);
}


// ─── AABB intersecting frustum ───────────────────────────────────────────────

TEST(RenderIRFrustum, AABBIntersectingFrustumIsEmitted)
{
    // unit_box at z=0.5: world bounds [0, 1] in z.
    // The far plane of the unit-cube frustum is at z=1, so this AABB straddles it.
    // The conservative p-vertex test should not cull it (min.z=0 → p_vertex inside far plane).
    auto cs = make_one_opaque(0.5f, unit_box(), unit_frustum_view());
    RenderIRStorage ir_st;
    auto ir = build_render_ir(ir_st, cs.scene);

    EXPECT_EQ(ir.opaque.size(), 1u) << "intersecting AABB was incorrectly culled";
    EXPECT_EQ(ir.culling.visible_opaque, 1u);
    EXPECT_EQ(ir.culling.culled_opaque,  0u);
}


// ─── DrawRef count equals visible primitive count ────────────────────────────

TEST(RenderIRFrustum, DrawRefCountEqualsVisibleCount)
{
    // Two primitives: one at z=0 (inside), one at z=10 (outside).
    auto scene = make_two_opaque(0.f, 10.f, unit_frustum_view());
    RenderIRStorage ir_st;
    auto ir = build_render_ir(ir_st, scene.compiled.scene);

    ASSERT_EQ(scene.compiled.scene.opaque.size(), 2u);
    EXPECT_EQ(ir.opaque.size(), 1u)
        << "DrawRef count should equal visible primitive count (1 of 2)";
    EXPECT_EQ(ir.culling.visible_opaque, 1u);
    EXPECT_EQ(ir.culling.culled_opaque,  1u);
}


// ─── Camera move exposes previously culled objects ────────────────────────────
//
// Regression test for the bug where update_render_ir() only re-sorted the
// previously visible set, making newly visible objects undiscoverable.

TEST(RenderIRFrustum, CameraMoveRevealsPreviouslyCulledObject)
{
    // Initial camera: frustum covers z=[−1, +1].
    // Object A at z=0 → inside frustum → visible.
    // Object B at z=5 → outside frustum → culled.
    auto scene = make_two_opaque(0.f, 5.f, frustum_at_z(0.f));
    RenderIRStorage ir_st;
    build_render_ir(ir_st, scene.compiled.scene);

    ASSERT_EQ(ir_st.ir.opaque.size(), 1u) << "expected only A visible initially";
    EXPECT_EQ(ir_st.ir.culling.culled_opaque, 1u);

    // Move camera so frustum covers z=[4, 6].
    // Object B (z=5) now inside, Object A (z=0) now outside.
    update_view(scene.compiled, frustum_at_z(5.f));
    update_render_ir(ir_st, scene.compiled.scene);

    EXPECT_EQ(ir_st.ir.opaque.size(), 1u) << "expected exactly one visible after camera move";
    EXPECT_EQ(ir_st.ir.culling.visible_opaque, 1u);
    EXPECT_EQ(ir_st.ir.culling.culled_opaque,  1u);

    // The remaining visible DrawRef must point to object B (the one with material=2).
    const auto& ref = ir_st.ir.opaque[0];
    EXPECT_EQ(scene.compiled.scene.opaque[ref.index].material, 2u)
        << "expected object B (material=2) to be the visible one after camera move";
}


// ─── Mixed pipelines: each reports correct visible counts ────────────────────

TEST(RenderIRFrustum, MixedPipelineCullingCounts)
{
    // Scene with one visible and one culled object in each pipeline.
    // Opaque and transparent: AABB-tested; splats: point-tested.
    // We use unit-cube frustum (z=[-1,1]).

    SceneBuilder b;
    TransformNode root{}; root.local = Mat4::identity();
    NodeHandle root_h = add_node(b, root);

    auto add_child = [&](float z) -> NodeHandle {
        TransformNode n{}; n.local = translation_z(z);
        n.flags = TransformNodeFlag::RenderDomain;
        NodeHandle h = add_node(b, n);
        add_edge(b, root_h, h);
        return h;
    };

    NodeHandle op_in  = add_child(0.f);  // opaque inside
    NodeHandle op_out = add_child(10.f); // opaque outside
    NodeHandle tr_in  = add_child(0.f);  // transparent inside
    NodeHandle tr_out = add_child(10.f); // transparent outside
    NodeHandle sp_in  = add_child(0.f);  // splat inside
    NodeHandle sp_out = add_child(10.f); // splat outside

    auto s = build(std::move(b));
    assert(s.has_value());
    propagate_all(s->polytree);

    const uint32_t total = node_count(s->polytree);
    std::vector<RenderableDescriptor> descs(total);
    descs[root_h] = { classify_legacy_renderable(RenderPipeline::None) };
    descs[op_in]  = { classify_legacy_renderable(RenderPipeline::OpaqueGeometry),      1u, 1u, unit_box() };
    descs[op_out] = { classify_legacy_renderable(RenderPipeline::OpaqueGeometry),      2u, 2u, unit_box() };
    descs[tr_in]  = { classify_legacy_renderable(RenderPipeline::TransparentGeometry), 1u, 1u, unit_box() };
    descs[tr_out] = { classify_legacy_renderable(RenderPipeline::TransparentGeometry), 2u, 2u, unit_box() };
    descs[sp_in]  = { classify_legacy_renderable(RenderPipeline::Splat),
                      INVALID_MESH, INVALID_MATERIAL, {}, SplatDescriptor{{1,1,1},{0,0,0,1},{1,1,1},1.f} };
    descs[sp_out] = { classify_legacy_renderable(RenderPipeline::Splat),
                      INVALID_MESH, INVALID_MATERIAL, {}, SplatDescriptor{{1,1,1},{0,0,0,1},{1,1,1},1.f} };

    CompiledSceneStorage cs{};
    compile(cs, s->polytree, descs, {}, unit_frustum_view());

    RenderIRStorage ir_st;
    auto ir = build_render_ir(ir_st, cs.scene);

    EXPECT_EQ(ir.opaque.size(),      1u);
    EXPECT_EQ(ir.transparent.size(), 1u);
    EXPECT_EQ(ir.splats.size(),      1u);

    EXPECT_EQ(ir.culling.visible_opaque,      1u);
    EXPECT_EQ(ir.culling.culled_opaque,       1u);
    EXPECT_EQ(ir.culling.visible_transparent, 1u);
    EXPECT_EQ(ir.culling.culled_transparent,  1u);
    EXPECT_EQ(ir.culling.visible_splats,      1u);
    EXPECT_EQ(ir.culling.culled_splats,       1u);
}


// ─── Sort order preserved for visible primitives ──────────────────────────────

TEST(RenderIRFrustum, OpaqueSortOrderPreservedAfterCulling)
{
    // Three opaque primitives: materials 2, 0, 1 (unordered).
    // One outside the frustum (material=2, at z=10).
    // Visible set: materials 0 and 1 — should be sorted ascending.

    SceneBuilder b;
    TransformNode root{}; root.local = Mat4::identity();
    NodeHandle root_h = add_node(b, root);

    auto add_child = [&](float z) -> NodeHandle {
        TransformNode n{}; n.local = translation_z(z);
        n.flags = TransformNodeFlag::RenderDomain;
        NodeHandle h = add_node(b, n);
        add_edge(b, root_h, h);
        return h;
    };

    NodeHandle a = add_child(0.f);   // mat=2, inside
    NodeHandle c = add_child(0.f);   // mat=0, inside
    NodeHandle e = add_child(10.f);  // mat=1, outside

    auto s = build(std::move(b));
    assert(s.has_value());
    propagate_all(s->polytree);

    std::vector<RenderableDescriptor> descs(node_count(s->polytree));
    descs[root_h] = { classify_legacy_renderable(RenderPipeline::None) };
    descs[a] = { classify_legacy_renderable(RenderPipeline::OpaqueGeometry), 2u, 2u, unit_box() };
    descs[c] = { classify_legacy_renderable(RenderPipeline::OpaqueGeometry), 0u, 0u, unit_box() };
    descs[e] = { classify_legacy_renderable(RenderPipeline::OpaqueGeometry), 1u, 1u, unit_box() };

    CompiledSceneStorage cs{};
    compile(cs, s->polytree, descs, {}, unit_frustum_view());

    RenderIRStorage ir_st;
    auto ir = build_render_ir(ir_st, cs.scene);

    ASSERT_EQ(ir.opaque.size(), 2u);

    // Sort keys must be non-decreasing.
    EXPECT_LE(ir.opaque[0].sort_key, ir.opaque[1].sort_key);

    // Material=1 was at z=10 (outside) — must not appear in visible set.
    for (auto& ref : ir.opaque)
        EXPECT_NE(cs.scene.opaque[ref.index].material, 1u)
            << "culled primitive (material=1) appeared in visible set";
}


// ─── Splat bounds-based culling (issue #28) ──────────────────────────────────
//
// A splat-cloud renderable whose centre sits outside the frustum but whose
// AABB extends into the frustum must remain visible.  Pre-fix behaviour was
// to test only the per-splat centre; large clouds whose origin left the
// frustum would disappear even when most of them was still on screen.

namespace {
    // Build a scene with one splat-cloud primitive at the given z position,
    // with the given local AABB.
    CompiledSceneStorage make_one_splat(
        float z, AABB local_bounds, ViewData view)
    {
        SceneBuilder b;
        TransformNode root{}; root.local = Mat4::identity();
        NodeHandle root_h = add_node(b, root);
        TransformNode obj{}; obj.local = translation_z(z);
        obj.flags = TransformNodeFlag::RenderDomain;
        NodeHandle obj_h = add_node(b, obj);
        add_edge(b, root_h, obj_h);

        auto s = build(std::move(b));
        assert(s.has_value());
        propagate_all(s->polytree);

        std::vector<RenderableDescriptor> descs(node_count(s->polytree));
        descs[root_h] = { classify_legacy_renderable(RenderPipeline::None) };
        descs[obj_h]  = {
            .node_class   = classify_legacy_renderable(RenderPipeline::Splat),
            .local_bounds = local_bounds,
            .splat_data   = SplatDescriptor{
                .cloud_handle      = static_cast<SplatHandle>(1),
                .cloud_local_index = 0,
            },
            .visible = true,
        };

        CompiledSceneStorage cs{};
        compile(cs, s->polytree, descs, {}, view);
        return cs;
    }
}

TEST(RenderIRFrustum, SplatBoundsIntersectingFrustumIsEmitted)
{
    // Splat node centre at z=3 (well outside the unit-cube frustum) but a
    // 4-unit-wide local AABB that extends from z=1 to z=5 — so the AABB
    // intersects the far plane of the frustum at z=1.
    //
    // Pre-fix (point cull): primitive culled → fail.
    // Post-fix (AABB cull): primitive visible.
    const AABB wide_local{
        Vec3{-2.f, -2.f, -2.f},
        Vec3{ 2.f,  2.f,  2.f}
    };
    auto cs = make_one_splat(3.f, wide_local, unit_frustum_view());
    RenderIRStorage ir_st;
    auto ir = build_render_ir(ir_st, cs.scene);

    EXPECT_EQ(ir.splats.size(), 1u)
        << "splat with bounds intersecting frustum was culled "
           "(centre-only cull regression)";
    EXPECT_EQ(ir.culling.visible_splats, 1u);
    EXPECT_EQ(ir.culling.culled_splats,  0u);
}

TEST(RenderIRFrustum, SplatBoundsOutsideFrustumIsCulled)
{
    // Centre at z=10, small AABB — clearly outside.  Must cull.
    auto cs = make_one_splat(
        10.f,
        AABB{ Vec3{-0.1f,-0.1f,-0.1f}, Vec3{0.1f,0.1f,0.1f} },
        unit_frustum_view());
    RenderIRStorage ir_st;
    auto ir = build_render_ir(ir_st, cs.scene);

    EXPECT_EQ(ir.splats.size(), 0u)
        << "splat with bounds outside frustum was kept";
    EXPECT_EQ(ir.culling.visible_splats, 0u);
    EXPECT_EQ(ir.culling.culled_splats,  1u);
}

TEST(RenderIRFrustum, SplatDegenerateBoundsFallsBackToPoint)
{
    // local_bounds left default (min == max == 0): the cull path falls back
    // to the per-splat centre.  Centre at z=0 is inside the unit frustum.
    auto cs = make_one_splat(0.f, AABB{}, unit_frustum_view());
    RenderIRStorage ir_st;
    auto ir = build_render_ir(ir_st, cs.scene);

    EXPECT_EQ(ir.splats.size(), 1u)
        << "degenerate-bounds fallback should treat inside-centre as visible";
}
