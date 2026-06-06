#include <gtest/gtest.h>
#include <math/mat4.h>
#include <render/backend/stub_backend.h>
#include <render/frame/render_frame.h>
#include <render/ir/render_ir.h>
#include <scene/compile/scene_compiler.h>
#include <scene/compile/legacy_classification.h>

using namespace wz::render;
using namespace wz::render::backend;
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
        v.view_projection.m[0]  = 1e-4f;
        v.view_projection.m[5]  = 1e-4f;
        v.view_projection.m[10] = 1e-4f;
        v.view_projection.m[15] = 1.0f;
        v.camera_position = Vec3{ 0.f, 0.f, z };
        return v;
    }

    // Full pipeline helper — builds scene, compiles, builds IR, builds frame
    struct Pipeline {
        SceneStorage          scene;
        CompiledSceneStorage  compiled;
        RenderIRStorage       ir;
        RenderFrameStorage    frame;
        SubmitResult          result;
    };

    Pipeline make_pipeline(ViewData view = camera_at_z(0.f))
    {
        SceneBuilder b;

        auto make_node = [](float z, uint16_t flags = TransformNodeFlag::RenderDomain) {
            TransformNode n{};
            n.local = translation_z(z);
            n.flags = flags;
            return n;
            };

        auto root = add_node(b, make_node(0.f, 0));
        auto op0 = add_node(b, make_node(1.f)); // opaque   mat=0
        auto op1 = add_node(b, make_node(2.f)); // opaque   mat=2
        auto op2 = add_node(b, make_node(3.f)); // opaque   mat=1
        auto tr0 = add_node(b, make_node(5.f)); // transparent depth=5
        auto tr1 = add_node(b, make_node(2.f)); // transparent depth=2
        auto sp0 = add_node(b, make_node(4.f)); // splat depth=4
        auto sp1 = add_node(b, make_node(1.f)); // splat depth=1
        auto pa0 = add_node(b, make_node(3.f)); // particle depth=3

        for (auto n : { op0,op1,op2,tr0,tr1,sp0,sp1,pa0 })
            add_edge(b, root, n);

        auto storage = build(std::move(b));
        assert(storage.has_value());
        propagate_all(storage->polytree);

        std::vector<RenderableDescriptor> descs(9);
        descs[root] = { classify_legacy_renderable(RenderPipeline::None) };
        descs[op0] = { classify_legacy_renderable(RenderPipeline::OpaqueGeometry),      0u, 0u, unit_box() };
        descs[op1] = { classify_legacy_renderable(RenderPipeline::OpaqueGeometry),      2u, 2u, unit_box() };
        descs[op2] = { classify_legacy_renderable(RenderPipeline::OpaqueGeometry),      1u, 1u, unit_box() };
        descs[tr0] = { classify_legacy_renderable(RenderPipeline::TransparentGeometry), 0u, 0u, unit_box() };
        descs[tr1] = { classify_legacy_renderable(RenderPipeline::TransparentGeometry), 1u, 1u, unit_box() };
        descs[sp0] = { classify_legacy_renderable(RenderPipeline::Splat), INVALID_MESH, INVALID_MATERIAL,
                        {}, SplatDescriptor{{1,1,1},{0,0,0,1},{1,1,1},0.9f,0u} };
        descs[sp1] = { classify_legacy_renderable(RenderPipeline::Splat), INVALID_MESH, INVALID_MATERIAL,
                        {}, SplatDescriptor{{1,1,1},{0,0,0,1},{1,1,1},0.5f,1u} };
        descs[pa0] = { classify_legacy_renderable(RenderPipeline::Particle), 0u, 0u, unit_box() };

        Pipeline p{};
        p.scene = std::move(*storage);
        compile(p.compiled, p.scene.polytree, descs, {}, view);
        build_render_ir(p.ir, p.compiled.scene);
        build_frame(p.frame, p.ir.ir, p.compiled.scene);
        p.result = submit(p.frame.frame);
        return p;
    }

} // namespace


// ─── Total command count ──────────────────────────────────────────────────────

TEST(StubBackendSpec, TotalCommandCountIsCorrect)
{
    auto p = make_pipeline();
    // 3 opaque + 2 splats + 2 transparent + 1 particle = 8
    EXPECT_EQ(p.result.total(), 8u);
}

TEST(StubBackendSpec, PerPipelineCountsAreCorrect)
{
    auto p = make_pipeline();
    EXPECT_EQ(p.result.opaque_count(), 3u);
    EXPECT_EQ(p.result.splat_count(), 2u);
    EXPECT_EQ(p.result.transparent_count(), 2u);
    EXPECT_EQ(p.result.particle_count(), 1u);
}


// ─── Section sizes ────────────────────────────────────────────────────────────

TEST(StubBackendSpec, OpaqueSectionHasCorrectSize)
{
    auto p = make_pipeline();
    EXPECT_EQ(p.frame.frame.opaque.size(), 3u);
}

TEST(StubBackendSpec, SplatSectionHasCorrectSize)
{
    auto p = make_pipeline();
    EXPECT_EQ(p.frame.frame.splats.size(), 2u);
}

TEST(StubBackendSpec, TransparentSectionHasCorrectSize)
{
    auto p = make_pipeline();
    EXPECT_EQ(p.frame.frame.transparent.size(), 2u);
}

TEST(StubBackendSpec, ParticleSectionHasCorrectSize)
{
    auto p = make_pipeline();
    EXPECT_EQ(p.frame.frame.particles.size(), 1u);
}


// ─── Section stage correctness ────────────────────────────────────────────────

TEST(StubBackendSpec, AllOpaqueCommandsHaveOpaqueStage)
{
    auto p = make_pipeline();
    for (uint32_t i = 0; i < p.frame.frame.opaque.size(); ++i)
        EXPECT_EQ(p.frame.frame.opaque[i].stage, PipelineStage::OpaqueGeometry)
            << "opaque[" << i << "] has wrong stage";
}

TEST(StubBackendSpec, AllSplatCommandsHaveSplatStage)
{
    auto p = make_pipeline();
    for (uint32_t i = 0; i < p.frame.frame.splats.size(); ++i)
        EXPECT_EQ(p.frame.frame.splats[i].stage, PipelineStage::Splat)
            << "splats[" << i << "] has wrong stage";
}

TEST(StubBackendSpec, AllTransparentCommandsHaveTransparentStage)
{
    auto p = make_pipeline();
    for (uint32_t i = 0; i < p.frame.frame.transparent.size(); ++i)
        EXPECT_EQ(p.frame.frame.transparent[i].stage, PipelineStage::TransparentGeometry)
            << "transparent[" << i << "] has wrong stage";
}

TEST(StubBackendSpec, AllParticleCommandsHaveParticleStage)
{
    auto p = make_pipeline();
    for (uint32_t i = 0; i < p.frame.frame.particles.size(); ++i)
        EXPECT_EQ(p.frame.frame.particles[i].stage, PipelineStage::Particle)
            << "particles[" << i << "] has wrong stage";
}


// ─── Sort order preserved through frame ───────────────────────────────────────

TEST(StubBackendSpec, OpaqueCommandsSortedByMaterial)
{
    auto p = make_pipeline();
    for (uint32_t i = 1; i < p.frame.frame.opaque.size(); ++i)
        EXPECT_LE(p.frame.frame.opaque[i - 1].sort_key,
                  p.frame.frame.opaque[i].sort_key)
            << "opaque sort broken at index " << i;
}

TEST(StubBackendSpec, TransparentCommandsSortedBackToFront)
{
    auto p = make_pipeline(camera_at_z(0.f));
    const auto& tr = p.frame.frame.transparent;
    for (uint32_t i = 1; i < tr.size(); ++i)
        EXPECT_LE(tr[i - 1].sort_key, tr[i].sort_key)
            << "transparent sort broken at index " << i;
}

TEST(StubBackendSpec, SplatCommandsSortedBackToFront)
{
    auto p = make_pipeline(camera_at_z(0.f));
    const auto& sp = p.frame.frame.splats;
    for (uint32_t i = 1; i < sp.size(); ++i)
        EXPECT_LE(sp[i - 1].sort_key, sp[i].sort_key)
            << "splat sort broken at index " << i;
}


// ─── Data integrity through the pipeline ─────────────────────────────────────

TEST(StubBackendSpec, SplatCommandCarriesCorrectData)
{
    // Splat nodes now use cloud handles (per-cloud path). Each DrawCommand is
    // a full-cloud draw: splats_buffer selects the resource, and an empty
    // sorted_splat_indices span tells the backend to use identity order.
    auto p = make_pipeline(camera_at_z(0.f));

    ASSERT_EQ(p.frame.frame.splats.size(), 2u);

    bool has_cloud_0 = false, has_cloud_1 = false;
    for (const auto& cmd : p.frame.frame.splats) {
        EXPECT_NE(cmd.splats_buffer, INVALID_SPLAT) << "expected per-cloud command";
        EXPECT_EQ(cmd.kind, DrawCommandKind::GaussianSplats);
        EXPECT_EQ(cmd.splat_instance_count, 0u) << "full-cloud draw";
        EXPECT_TRUE(cmd.sorted_splat_indices.empty()) << "full-cloud draw";
        if (cmd.splats_buffer == 0u) has_cloud_0 = true;
        if (cmd.splats_buffer == 1u) has_cloud_1 = true;
    }
    EXPECT_TRUE(has_cloud_0) << "cloud handle 0 not found in splat commands";
    EXPECT_TRUE(has_cloud_1) << "cloud handle 1 not found in splat commands";
}

TEST(StubBackendSpec, OpaqueCommandCarriesCorrectMesh)
{
    auto p = make_pipeline();

    std::vector<uint32_t> meshes;
    for (auto& cmd : p.frame.frame.opaque)
        meshes.push_back(cmd.mesh);

    ASSERT_EQ(meshes.size(), 3u);
    for (uint32_t m : {0u, 1u, 2u})
        EXPECT_NE(std::find(meshes.begin(), meshes.end(), m), meshes.end())
            << "mesh " << m << " missing from opaque commands";
}

TEST(StubBackendSpec, ViewDataReachesFrame)
{
    auto p = make_pipeline(camera_at_z(7.f));
    EXPECT_FLOAT_EQ(p.frame.frame.view.camera_position.z, 7.f);
}


// ─── submit() — log and counts ────────────────────────────────────────────────

TEST(StubBackendSpec, LogHasOneEntryPerCommand)
{
    auto p = make_pipeline();
    EXPECT_EQ(p.result.log.size(), p.result.total());
}

TEST(StubBackendSpec, LogEntriesAreNonEmpty)
{
    auto p = make_pipeline();
    for (auto& entry : p.result.log)
        EXPECT_FALSE(entry.empty());
}


// ─── Full pipeline end-to-end ─────────────────────────────────────────────────

TEST(StubBackendSpec, EndToEndEmptyScene)
{
    SceneBuilder b;
    TransformNode root{};
    add_node(b, root);
    auto s = build(std::move(b));
    ASSERT_TRUE(s.has_value());
    propagate_all(s->polytree);

    std::vector<RenderableDescriptor> descs(1);
    descs[0] = { classify_legacy_renderable(RenderPipeline::None) };

    CompiledSceneStorage compiled;
    RenderIRStorage      ir_storage;
    RenderFrameStorage   frame_storage;

    auto cs    = compile(compiled, s->polytree, descs, {}, camera_at_z(0.f));
    auto ir    = build_render_ir(ir_storage, cs);
    auto frame = build_frame(frame_storage, ir, cs);
    auto result = submit(frame);

    EXPECT_EQ(result.total(), 0u);
    EXPECT_TRUE(result.log.empty());
}
