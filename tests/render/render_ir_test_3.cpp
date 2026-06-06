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

    ViewData camera_at_z(float z)
    {
        ViewData v{};
        v.view = Mat4::identity();
        v.view.m[14] = -z;
        v.projection = Mat4::identity();
        // Wide orthographic VP: frustum ≈ ±10000 so test objects at z=1–10 are never culled.
        // Depth computation uses v.view, not view_projection.
        v.view_projection.m[0]  = 1e-4f;
        v.view_projection.m[5]  = 1e-4f;
        v.view_projection.m[10] = 1e-4f;
        v.view_projection.m[15] = 1.0f;
        v.camera_position = Vec3{ 0.f, 0.f, z };
        return v;
    }

    // Build a compiled scene with:
    //   3 opaque nodes  — materials 2, 0, 1 (deliberately unordered)
    //   3 transparent   — depths 1, 3, 2    (deliberately unordered)
    //   3 splats        — at z=5, z=1, z=3  (deliberately unordered)
    //   2 particles     — at z=2, z=4

    CompiledSceneStorage make_compiled_scene(ViewData view = camera_at_z(0.f))
    {
        SceneBuilder b;

        auto make_node = [](float z) {
            TransformNode n{};
            n.local = translation_z(z);
            n.flags = TransformNodeFlag::RenderDomain;
            return n;
            };

        auto root = add_node(b, [] { TransformNode n{}; return n; }());

        // Opaque nodes — materials 2, 0, 1
        auto op0 = add_node(b, make_node(1.f));
        auto op1 = add_node(b, make_node(2.f));
        auto op2 = add_node(b, make_node(3.f));

        // Transparent nodes — z positions give depths when camera at z=0
        auto tr0 = add_node(b, make_node(1.f));
        auto tr1 = add_node(b, make_node(3.f));
        auto tr2 = add_node(b, make_node(2.f));

        // Splat nodes
        auto sp0 = add_node(b, make_node(5.f));
        auto sp1 = add_node(b, make_node(1.f));
        auto sp2 = add_node(b, make_node(3.f));

        // Particle nodes
        auto pa0 = add_node(b, make_node(2.f));
        auto pa1 = add_node(b, make_node(4.f));

        for (auto n : { op0,op1,op2,tr0,tr1,tr2,sp0,sp1,sp2,pa0,pa1 })
            add_edge(b, root, n);

        auto storage = build(std::move(b));
        assert(storage.has_value());
        propagate_all(storage->polytree);

        std::vector<RenderableDescriptor> descs(12);
        descs[root] = { classify_legacy_renderable(RenderPipeline::None) };
        descs[op0] = { classify_legacy_renderable(RenderPipeline::OpaqueGeometry),      2u, 2u, unit_box() };
        descs[op1] = { classify_legacy_renderable(RenderPipeline::OpaqueGeometry),      0u, 0u, unit_box() };
        descs[op2] = { classify_legacy_renderable(RenderPipeline::OpaqueGeometry),      1u, 1u, unit_box() };
        descs[tr0] = { classify_legacy_renderable(RenderPipeline::TransparentGeometry), 0u, 0u, unit_box() };
        descs[tr1] = { classify_legacy_renderable(RenderPipeline::TransparentGeometry), 1u, 1u, unit_box() };
        descs[tr2] = { classify_legacy_renderable(RenderPipeline::TransparentGeometry), 2u, 2u, unit_box() };
        descs[sp0] = { classify_legacy_renderable(RenderPipeline::Splat), INVALID_MESH, INVALID_MATERIAL,
                        {}, SplatDescriptor{{1,1,1},{0,0,0,1},{1,1,1},1.f} };
        descs[sp1] = { classify_legacy_renderable(RenderPipeline::Splat), INVALID_MESH, INVALID_MATERIAL,
                        {}, SplatDescriptor{{1,1,1},{0,0,0,1},{1,1,1},1.f} };
        descs[sp2] = { classify_legacy_renderable(RenderPipeline::Splat), INVALID_MESH, INVALID_MATERIAL,
                        {}, SplatDescriptor{{1,1,1},{0,0,0,1},{1,1,1},1.f} };
        descs[pa0] = { classify_legacy_renderable(RenderPipeline::Particle), 0u, 0u, unit_box() };
        descs[pa1] = { classify_legacy_renderable(RenderPipeline::Particle), 1u, 1u, unit_box() };

        CompiledSceneStorage cs{};
        compile(cs, storage->polytree, descs, {}, view);
        return cs;
    }

} // namespace


// ─── DrawRef counts ───────────────────────────────────────────────────────────

TEST(RenderIRSpec, OpaqueRefCountMatchesPrimitives)
{
    auto cs = make_compiled_scene();
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);
    EXPECT_EQ(ir.opaque.size(), cs.scene.opaque.size());
}

TEST(RenderIRSpec, TransparentRefCountMatchesPrimitives)
{
    auto cs = make_compiled_scene();
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);
    EXPECT_EQ(ir.transparent.size(), cs.scene.transparent.size());
}

TEST(RenderIRSpec, SplatRefCountMatchesPrimitives)
{
    auto cs = make_compiled_scene();
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);
    EXPECT_EQ(ir.splats.size(), cs.scene.splats.size());
}

TEST(RenderIRSpec, ParticleRefCountMatchesPrimitives)
{
    auto cs = make_compiled_scene();
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);
    EXPECT_EQ(ir.particles.size(), cs.scene.particles.size());
}


// ─── Opaque sort — ascending material key ────────────────────────────────────

TEST(RenderIRSpec, OpaqueSortedByMaterial)
{
    auto cs = make_compiled_scene();
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);

    // Keys must be non-decreasing
    for (uint32_t i = 1; i < ir.opaque.size(); ++i)
        EXPECT_LE(ir.opaque[i - 1].sort_key, ir.opaque[i].sort_key)
        << "opaque sort broken at index " << i;
}

TEST(RenderIRSpec, OpaqueAllIndicesPresent)
{
    auto cs = make_compiled_scene();
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);

    // Every index 0..N-1 appears exactly once
    std::vector<uint32_t> seen(ir.opaque.size(), 0);
    for (auto& ref : ir.opaque) {
        ASSERT_LT(ref.index, seen.size());
        ++seen[ref.index];
    }
    for (auto c : seen) EXPECT_EQ(c, 1u);
}


// ─── Transparent sort — back-to-front ────────────────────────────────────────

TEST(RenderIRSpec, TransparentSortedBackToFront)
{
    auto cs = make_compiled_scene(camera_at_z(0.f));
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);

    // Keys non-decreasing = depths non-increasing (further first)
    for (uint32_t i = 1; i < ir.transparent.size(); ++i)
        EXPECT_LE(ir.transparent[i - 1].sort_key, ir.transparent[i].sort_key)
        << "transparent sort broken at index " << i;
}

TEST(RenderIRSpec, TransparentFurthestFirst)
{
    auto cs = make_compiled_scene(camera_at_z(0.f));
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);

    // First ref should point to the deepest (furthest) transparent primitive
    const auto& first = cs.scene.transparent[ir.transparent[0].index];
    for (auto& ref : ir.transparent) {
        const auto& p = cs.scene.transparent[ref.index];
        EXPECT_LE(first.depth, p.depth)
            << "first transparent is not the furthest";
    }
}

TEST(RenderIRSpec, TransparentAllIndicesPresent)
{
    auto cs = make_compiled_scene();
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);

    std::vector<uint32_t> seen(ir.transparent.size(), 0);
    for (auto& ref : ir.transparent) {
        ASSERT_LT(ref.index, seen.size());
        ++seen[ref.index];
    }
    for (auto c : seen) EXPECT_EQ(c, 1u);
}


// ─── Splat sort — back-to-front ───────────────────────────────────────────────

TEST(RenderIRSpec, SplatsSortedBackToFront)
{
    auto cs = make_compiled_scene(camera_at_z(0.f));
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);

    for (uint32_t i = 1; i < ir.splats.size(); ++i)
        EXPECT_LE(ir.splats[i - 1].sort_key, ir.splats[i].sort_key)
        << "splat sort broken at index " << i;
}

TEST(RenderIRSpec, SplatFurthestFirst)
{
    auto cs = make_compiled_scene(camera_at_z(0.f));
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);

    // First splat ref should point to deepest splat
    float first_depth = cs.scene.splat_depths[ir.splats[0].index];
    for (auto& ref : ir.splats)
        EXPECT_LE(first_depth, cs.scene.splat_depths[ref.index]);
}

TEST(RenderIRSpec, SplatAllIndicesPresent)
{
    auto cs = make_compiled_scene();
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);

    std::vector<uint32_t> seen(ir.splats.size(), 0);
    for (auto& ref : ir.splats) {
        ASSERT_LT(ref.index, seen.size());
        ++seen[ref.index];
    }
    for (auto c : seen) EXPECT_EQ(c, 1u);
}


// ─── update_render_ir() — re-sort after camera move ──────────────────────────

TEST(RenderIRSpec, UpdateRenderIRResortsSplats)
{
    auto cs = make_compiled_scene(camera_at_z(0.f));
    RenderIRStorage ir_storage;
    build_render_ir(ir_storage, cs.scene);

    // Record order before camera move
    std::vector<uint32_t> before;
    for (auto& ref : ir_storage.ir.splats) before.push_back(ref.index);

    // Move camera so depths change significantly
    update_view(cs, camera_at_z(10.f));
    update_render_ir(ir_storage, cs.scene);

    std::vector<uint32_t> after;
    for (auto& ref : ir_storage.ir.splats) after.push_back(ref.index);

    // Order must have changed — all splats shift relative to new camera
    EXPECT_NE(before, after);
}

TEST(RenderIRSpec, UpdateRenderIRMaintainsSplatCount)
{
    auto cs = make_compiled_scene(camera_at_z(0.f));
    RenderIRStorage ir_storage;
    build_render_ir(ir_storage, cs.scene);

    uint32_t count_before = static_cast<uint32_t>(ir_storage.ir.splats.size());
    update_view(cs, camera_at_z(10.f));
    update_render_ir(ir_storage, cs.scene);
    uint32_t count_after = static_cast<uint32_t>(ir_storage.ir.splats.size());

    EXPECT_EQ(count_before, count_after);
}

TEST(RenderIRSpec, UpdateRenderIRSplatsStillSortedAfterMove)
{
    auto cs = make_compiled_scene(camera_at_z(0.f));
    RenderIRStorage ir_storage;
    build_render_ir(ir_storage, cs.scene);

    update_view(cs, camera_at_z(6.f));
    update_render_ir(ir_storage, cs.scene);

    for (uint32_t i = 1; i < ir_storage.ir.splats.size(); ++i)
        EXPECT_LE(ir_storage.ir.splats[i - 1].sort_key, ir_storage.ir.splats[i].sort_key)
        << "splat sort broken after camera move at index " << i;
}

TEST(RenderIRSpec, UpdateRenderIRTransparentStillSortedAfterMove)
{
    auto cs = make_compiled_scene(camera_at_z(0.f));
    RenderIRStorage ir_storage;
    build_render_ir(ir_storage, cs.scene);

    update_view(cs, camera_at_z(6.f));
    update_render_ir(ir_storage, cs.scene);

    for (uint32_t i = 1; i < ir_storage.ir.transparent.size(); ++i)
        EXPECT_LE(ir_storage.ir.transparent[i - 1].sort_key, ir_storage.ir.transparent[i].sort_key)
        << "transparent sort broken after camera move at index " << i;
}


// ─── Empty pipelines ──────────────────────────────────────────────────────────

TEST(RenderIRSpec, EmptyCompiledSceneProducesEmptyIR)
{
    SceneBuilder b;
    TransformNode root{};
    add_node(b, root);
    auto s = build(std::move(b));
    ASSERT_TRUE(s.has_value());
    propagate_all(s->polytree);

    std::vector<RenderableDescriptor> descs(1);
    descs[0] = { classify_legacy_renderable(RenderPipeline::None) };
    CompiledSceneStorage cs{};
    compile(cs, s->polytree, descs, {}, camera_at_z(0.f));
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);

    EXPECT_EQ(ir.opaque.size(), 0u);
    EXPECT_EQ(ir.transparent.size(), 0u);
    EXPECT_EQ(ir.splats.size(), 0u);
    EXPECT_EQ(ir.particles.size(), 0u);
}

TEST(RenderIRSpec, SourceSpansPointIntoCompiledSceneStorage)
{
    auto cs = make_compiled_scene();
    RenderIRStorage ir_storage;
    auto ir = build_render_ir(ir_storage, cs.scene);
    // source spans must alias the same backing memory as cs.scene
    EXPECT_EQ(ir.source.opaque.data(), cs.scene.opaque.data());
}
