#include <gtest/gtest.h>
#include <engine/assets/terrain/terrain_visual_proxy.h>
#include <render/frame/render_frame.h>
#include <render/ir/render_ir.h>
#include <scene/compile/scene_compiler.h>
#include <scene/compile/legacy_classification.h>
#include <math/mat4.h>

using namespace wz::render;
using namespace wz::scene;
using namespace wz::core::graph;
using namespace wz::math;

// ─── Helpers ──────────────────────────────────────────────────────────────────

namespace {

    AABB unit_box()
    {
        return { Vec3{-0.5f,-0.5f,-0.5f}, Vec3{0.5f,0.5f,0.5f} };
    }

    wz::asset::AssetKey test_proxy_key()
    {
        return wz::asset::AssetKey{
            .content_hash = { 1, 2 },
            .schema_hash = { 3, 4 },
            .compiler_hash = { 5, 6 },
            .deps_hash = { 7, 8 },
        };
    }

    Mat4 translation_z(float z)
    {
        Mat4 m = Mat4::identity();
        m.m[14] = z;
        return m;
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

    void expect_same_command(const DrawCommand& a, const DrawCommand& b)
    {
        EXPECT_EQ(a.stage, b.stage);
        EXPECT_EQ(a.kind, b.kind);
        EXPECT_EQ(a.mesh, b.mesh);
        EXPECT_EQ(a.material, b.material);
        EXPECT_EQ(a.splats_buffer, b.splats_buffer);
        EXPECT_EQ(a.sort_key, b.sort_key);
        EXPECT_EQ(a.splat_instance_count, b.splat_instance_count);
        ASSERT_EQ(a.sorted_splat_indices.size(), b.sorted_splat_indices.size());

        for (uint32_t i = 0; i < 16u; ++i)
            EXPECT_FLOAT_EQ(a.world.m[i], b.world.m[i]) << "world[" << i << "]";

        for (uint32_t i = 0; i < static_cast<uint32_t>(a.sorted_splat_indices.size()); ++i)
            EXPECT_EQ(a.sorted_splat_indices[i], b.sorted_splat_indices[i])
                << "sorted_splat_indices[" << i << "]";

        EXPECT_FLOAT_EQ(a.splat_position.x, b.splat_position.x);
        EXPECT_FLOAT_EQ(a.splat_position.y, b.splat_position.y);
        EXPECT_FLOAT_EQ(a.splat_position.z, b.splat_position.z);
        EXPECT_FLOAT_EQ(a.splat_scale.x, b.splat_scale.x);
        EXPECT_FLOAT_EQ(a.splat_scale.y, b.splat_scale.y);
        EXPECT_FLOAT_EQ(a.splat_scale.z, b.splat_scale.z);
        EXPECT_FLOAT_EQ(a.splat_rotation.x, b.splat_rotation.x);
        EXPECT_FLOAT_EQ(a.splat_rotation.y, b.splat_rotation.y);
        EXPECT_FLOAT_EQ(a.splat_rotation.z, b.splat_rotation.z);
        EXPECT_FLOAT_EQ(a.splat_rotation.w, b.splat_rotation.w);
        EXPECT_FLOAT_EQ(a.splat_color.x, b.splat_color.x);
        EXPECT_FLOAT_EQ(a.splat_color.y, b.splat_color.y);
        EXPECT_FLOAT_EQ(a.splat_color.z, b.splat_color.z);
        EXPECT_FLOAT_EQ(a.splat_opacity, b.splat_opacity);
        EXPECT_FLOAT_EQ(a.splat_depth, b.splat_depth);
    }

    void expect_same_commands(
        std::span<const DrawCommand> a,
        std::span<const DrawCommand> b)
    {
        ASSERT_EQ(a.size(), b.size());
        for (uint32_t i = 0; i < static_cast<uint32_t>(a.size()); ++i) {
            SCOPED_TRACE(i);
            expect_same_command(a[i], b[i]);
        }
    }

    void expect_same_frame_sections(RenderFrameView a, RenderFrameView b)
    {
        ASSERT_EQ(a.sky.size(), b.sky.size());
        for (uint32_t i = 0; i < static_cast<uint32_t>(a.sky.size()); ++i) {
            SCOPED_TRACE(i);
            EXPECT_EQ(a.sky[i].stage, b.sky[i].stage);
            EXPECT_EQ(a.sky[i].visual_kind, b.sky[i].visual_kind);
            EXPECT_EQ(a.sky[i].projection, b.sky[i].projection);
            EXPECT_FLOAT_EQ(a.sky[i].solid_color.x, b.sky[i].solid_color.x);
            EXPECT_FLOAT_EQ(a.sky[i].solid_color.y, b.sky[i].solid_color.y);
            EXPECT_FLOAT_EQ(a.sky[i].solid_color.z, b.sky[i].solid_color.z);
            EXPECT_FLOAT_EQ(a.sky[i].exposure, b.sky[i].exposure);
        }
        expect_same_commands(a.opaque, b.opaque);
        ASSERT_EQ(a.terrain_instances.size(), b.terrain_instances.size());
        for (uint32_t i = 0;
             i < static_cast<uint32_t>(a.terrain_instances.size());
             ++i)
        {
            SCOPED_TRACE(i);
            EXPECT_EQ(
                a.terrain_instances[i].terrain_proxy_id,
                b.terrain_instances[i].terrain_proxy_id);
            EXPECT_EQ(
                a.terrain_instances[i].visual_proxy_asset,
                b.terrain_instances[i].visual_proxy_asset);
            EXPECT_EQ(a.terrain_instances[i].material, b.terrain_instances[i].material);
            EXPECT_EQ(a.terrain_instances[i].visible, b.terrain_instances[i].visible);
            for (uint32_t m = 0; m < 16u; ++m)
                EXPECT_FLOAT_EQ(
                    a.terrain_instances[i].world.m[m],
                    b.terrain_instances[i].world.m[m]);
        }
        expect_same_commands(a.splats, b.splats);
        expect_same_commands(a.transparent, b.transparent);
        expect_same_commands(a.particles, b.particles);
    }

    // Mixed scene: 2 opaque, 2 transparent, 2 splats, 1 particle.
    struct MixedPipeline {
        SceneStorage         scene{};
        CompiledSceneStorage compiled{};
        RenderIRStorage      ir{};
        RenderFrameStorage   frame{};
    };

    MixedPipeline make_mixed(ViewData view = camera_at_z(0.f))
    {
        SceneBuilder b;

        auto make_node = [](float z) {
            TransformNode n{};
            n.local = translation_z(z);
            n.flags = TransformNodeFlag::RenderDomain;
            return n;
        };

        auto root = add_node(b, [] { TransformNode n{}; return n; }());
        auto op0 = add_node(b, make_node(1.f));
        auto op1 = add_node(b, make_node(2.f));
        auto tr0 = add_node(b, make_node(3.f));
        auto tr1 = add_node(b, make_node(5.f));
        auto sp0 = add_node(b, make_node(4.f));
        auto sp1 = add_node(b, make_node(2.f));
        auto pa0 = add_node(b, make_node(3.f));

        for (auto n : { op0, op1, tr0, tr1, sp0, sp1, pa0 })
            add_edge(b, root, n);

        auto result = build(std::move(b));
        assert(result.has_value());
        propagate_all(result->polytree);

        std::vector<RenderableDescriptor> descs(8);
        descs[root] = { classify_legacy_renderable(RenderPipeline::None) };
        descs[op0]  = { classify_legacy_renderable(RenderPipeline::OpaqueGeometry),      1u, 1u, unit_box() };
        descs[op1]  = { classify_legacy_renderable(RenderPipeline::OpaqueGeometry),      2u, 2u, unit_box() };
        descs[tr0]  = { classify_legacy_renderable(RenderPipeline::TransparentGeometry), 1u, 1u, unit_box() };
        descs[tr1]  = { classify_legacy_renderable(RenderPipeline::TransparentGeometry), 2u, 2u, unit_box() };
        descs[sp0]  = { classify_legacy_renderable(RenderPipeline::Splat),
                        INVALID_MESH, INVALID_MATERIAL, {}, SplatDescriptor{{1,1,1},{0,0,0,1},{1,1,1},0.8f,0u} };
        descs[sp1]  = { classify_legacy_renderable(RenderPipeline::Splat),
                        INVALID_MESH, INVALID_MATERIAL, {}, SplatDescriptor{{1,1,1},{0,0,0,1},{1,1,1},0.4f,1u} };
        descs[pa0]  = { classify_legacy_renderable(RenderPipeline::Particle), 1u, 1u, unit_box() };

        MixedPipeline p{};
        p.scene = std::move(*result);
        compile(p.compiled, p.scene.polytree, descs, {}, view);
        build_render_ir(p.ir, p.compiled.scene);
        build_frame(p.frame, p.ir.ir, p.compiled.scene);
        return p;
    }

    CompiledSceneStorage make_terrain_scene(ViewData view = camera_at_z(0.f))
    {
        SceneBuilder b;
        TransformNode root{};
        NodeHandle root_h = add_node(b, root);

        TransformNode terrain_node{};
        terrain_node.local = translation_z(1.0f);
        terrain_node.flags = TransformNodeFlag::RenderDomain;
        NodeHandle terrain_h = add_node(b, terrain_node);
        add_edge(b, root_h, terrain_h);

        auto storage = build(std::move(b));
        assert(storage.has_value());
        propagate_all(storage->polytree);

        const wz::asset::AssetKey proxy_key = test_proxy_key();
        std::vector<RenderableDescriptor> descs(node_count(storage->polytree));
        descs[root_h] = { classify_legacy_renderable(RenderPipeline::None) };
        descs[terrain_h] = RenderableDescriptor{
            .node_class = SceneNodeClass{
                .role = SceneRole::Renderable,
                .producer = ProducerKind::TerrainPatch,
                .default_surface = SurfaceClass::Opaque,
                .spatial = SpatialKind::Box,
                .compile = CompileBehavior::Static,
                .domains = static_cast<RenderDomainMask>(RenderDomain::Surface),
            },
            .material = 9u,
            .local_bounds = unit_box(),
            .terrain_visual_proxy_asset = proxy_key,
            .terrain_proxy_id =
                TerrainProxyId{ proxy_key },
            .visible = true,
        };

        CompiledSceneStorage cs{};
        compile(cs, storage->polytree, descs, {}, view);
        return cs;
    }

    std::vector<TerrainLodChoice> make_terrain_choices(uint32_t first_chunk = 7u)
    {
        return {
            TerrainLodChoice{
                .terrain_instance_index = 0u,
                .chunk_id = TerrainChunkId{ first_chunk },
                .representation_kind =
                    TerrainVisualRepresentationKind::MeshChunks,
                .lod_id = TerrainLodId{ 1 },
                .projected_error_px = 0.5f,
                .projected_area_px = 128.0f,
                .priority = 2.0f,
            },
            TerrainLodChoice{
                .terrain_instance_index = 0u,
                .chunk_id = TerrainChunkId{ first_chunk + 1u },
                .representation_kind =
                    TerrainVisualRepresentationKind::MeshChunks,
                .lod_id = TerrainLodId{ 0 },
                .projected_error_px = 0.1f,
                .projected_area_px = 64.0f,
                .priority = 1.0f,
            },
        };
    }

} // namespace


// ─── Section sizes match IR pipeline counts ───────────────────────────────────

TEST(SectionedRenderFrame, OpaqueSectionSizeMatchesIR)
{
    auto p = make_mixed();
    EXPECT_EQ(p.frame.frame.opaque.size(),      p.ir.ir.opaque.size());
}

TEST(SectionedRenderFrame, SplatSectionSizeMatchesIR)
{
    auto p = make_mixed();
    EXPECT_EQ(p.frame.frame.splats.size(),      p.ir.ir.splats.size());
}

TEST(SectionedRenderFrame, TransparentSectionSizeMatchesIR)
{
    auto p = make_mixed();
    EXPECT_EQ(p.frame.frame.transparent.size(), p.ir.ir.transparent.size());
}

TEST(SectionedRenderFrame, ParticleSectionSizeMatchesIR)
{
    auto p = make_mixed();
    EXPECT_EQ(p.frame.frame.particles.size(),   p.ir.ir.particles.size());
}

TEST(SectionedRenderFrame, TerrainSectionSizeMatchesIR)
{
    auto cs = make_terrain_scene();
    auto choices = make_terrain_choices();
    CompiledSceneView scene = cs.scene;
    scene.terrain_lod_choices = std::span<const TerrainLodChoice>(choices);

    RenderIRStorage ir_st{};
    RenderFrameStorage fr_st{};
    build_render_ir(ir_st, scene);
    build_frame(fr_st, ir_st.ir, scene);

    EXPECT_EQ(fr_st.frame.terrain.size(), ir_st.ir.terrain.size());
}

TEST(SectionedRenderFrame, TerrainInstancesCopiedIntoFrame)
{
    auto cs = make_terrain_scene();
    auto choices = make_terrain_choices();
    CompiledSceneView scene = cs.scene;
    scene.terrain_lod_choices = std::span<const TerrainLodChoice>(choices);

    RenderIRStorage ir_st{};
    RenderFrameStorage fr_st{};
    build_render_ir(ir_st, scene);
    build_frame(fr_st, ir_st.ir, scene);

    ASSERT_EQ(fr_st.frame.terrain_instances.size(), 1u);
    EXPECT_EQ(
        fr_st.frame.terrain_instances[0].terrain_proxy_id,
        cs.scene.terrain_instances[0].terrain_proxy_id);
    EXPECT_NE(
        fr_st.frame.terrain_instances.data(),
        cs.scene.terrain_instances.data())
        << "RenderFrame must own terrain instance storage";
}

TEST(SectionedRenderFrame, TerrainDrawRefsCopiedIntoFrame)
{
    auto cs = make_terrain_scene();
    auto choices = make_terrain_choices();
    CompiledSceneView scene = cs.scene;
    scene.terrain_lod_choices = std::span<const TerrainLodChoice>(choices);

    RenderIRStorage ir_st{};
    RenderFrameStorage fr_st{};
    build_render_ir(ir_st, scene);
    build_frame(fr_st, ir_st.ir, scene);

    ASSERT_EQ(fr_st.frame.terrain.size(), 2u);
    EXPECT_EQ(fr_st.frame.terrain[0].terrain_instance_index, 0u);
    EXPECT_EQ(fr_st.frame.terrain[0].chunk_id, ir_st.ir.terrain[0].chunk_id);
    EXPECT_EQ(fr_st.frame.terrain[0].lod_id, ir_st.ir.terrain[0].lod_id);
    EXPECT_NE(fr_st.frame.terrain.data(), ir_st.ir.terrain.data())
        << "RenderFrame must own its terrain ref storage";
}

TEST(SectionedRenderFrame, UpdateFrameViewRebuildsTerrainSection)
{
    auto cs = make_terrain_scene();
    auto choices = make_terrain_choices(7u);
    CompiledSceneView scene = cs.scene;
    scene.terrain_lod_choices = std::span<const TerrainLodChoice>(choices);

    RenderIRStorage ir_st{};
    RenderFrameStorage fr_st{};
    build_render_ir(ir_st, scene);
    build_frame(fr_st, ir_st.ir, scene);
    ASSERT_EQ(fr_st.frame.terrain.size(), 2u);

    auto updated_choices = make_terrain_choices(20u);
    scene.terrain_lod_choices = std::span<const TerrainLodChoice>(updated_choices);
    update_render_ir(ir_st, scene);
    update_frame_view(fr_st, ir_st.ir, scene);

    ASSERT_EQ(fr_st.frame.terrain.size(), 2u);
    EXPECT_EQ(fr_st.frame.terrain[0].chunk_id, ir_st.ir.terrain[0].chunk_id);
    EXPECT_EQ(fr_st.frame.terrain[1].chunk_id, ir_st.ir.terrain[1].chunk_id);
    EXPECT_NE(fr_st.frame.terrain[0].chunk_id, choices[0].chunk_id);
}

TEST(SectionedRenderFrame, BuildFrameCopiesSkyCommands)
{
    auto p = make_mixed();
    const SkyDrawCommand sky{
        .visual_kind = SkyVisualKind::SolidColor,
        .projection = SkyProjection::Sphere,
        .solid_color = Vec3{ 0.15f, 0.25f, 0.35f },
        .gradient_top_color = Vec3{ 0.8f, 0.9f, 1.0f },
        .gradient_bottom_color = Vec3{ 0.1f, 0.2f, 0.3f },
        .exposure = 1.5f,
        .scalar_field_handle = 42u,
        .vector_field_handle = 43u,
    };

    build_frame(p.frame, p.ir.ir, p.compiled.scene, std::span{ &sky, 1u });

    ASSERT_EQ(p.frame.frame.sky.size(), 1u);
    EXPECT_EQ(p.frame.frame.sky[0].stage, PipelineStage::Sky);
    EXPECT_EQ(p.frame.frame.sky[0].visual_kind, SkyVisualKind::SolidColor);
    EXPECT_EQ(p.frame.frame.sky[0].projection, SkyProjection::Sphere);
    EXPECT_FLOAT_EQ(p.frame.frame.sky[0].solid_color.x, 0.15f);
    EXPECT_FLOAT_EQ(p.frame.frame.sky[0].solid_color.y, 0.25f);
    EXPECT_FLOAT_EQ(p.frame.frame.sky[0].solid_color.z, 0.35f);
    EXPECT_FLOAT_EQ(p.frame.frame.sky[0].gradient_top_color.x, 0.8f);
    EXPECT_FLOAT_EQ(p.frame.frame.sky[0].gradient_bottom_color.z, 0.3f);
    EXPECT_FLOAT_EQ(p.frame.frame.sky[0].exposure, 1.5f);
    EXPECT_EQ(p.frame.frame.sky[0].scalar_field_handle, 42u);
    EXPECT_EQ(p.frame.frame.sky[0].vector_field_handle, 43u);
}

TEST(SectionedRenderFrame, UpdateFrameViewPreservesSkySection)
{
    auto p = make_mixed(camera_at_z(0.f));
    const SkyDrawCommand sky{
        .visual_kind = SkyVisualKind::SolidColor,
        .solid_color = Vec3{ 0.1f, 0.2f, 0.3f },
    };

    build_frame(p.frame, p.ir.ir, p.compiled.scene, std::span{ &sky, 1u });
    const SkyDrawCommand* sky_data = p.frame.frame.sky.data();

    update_view(p.compiled, camera_at_z(10.f));
    update_render_ir(p.ir, p.compiled.scene);
    update_frame_view(p.frame, p.ir.ir, p.compiled.scene);

    ASSERT_EQ(p.frame.frame.sky.size(), 1u);
    EXPECT_EQ(p.frame.frame.sky.data(), sky_data);
    EXPECT_EQ(p.frame.frame.sky[0].visual_kind, SkyVisualKind::SolidColor);
    EXPECT_FLOAT_EQ(p.frame.frame.sky[0].solid_color.z, 0.3f);
}


// ─── Section stage tags are correct ──────────────────────────────────────────

TEST(SectionedRenderFrame, OpaqueCommandsTaggedOpaque)
{
    auto p = make_mixed();
    for (auto& cmd : p.frame.frame.opaque)
        EXPECT_EQ(cmd.stage, PipelineStage::OpaqueGeometry);
}

TEST(SectionedRenderFrame, SplatCommandsTaggedSplat)
{
    auto p = make_mixed();
    for (auto& cmd : p.frame.frame.splats)
        EXPECT_EQ(cmd.stage, PipelineStage::Splat);
}

TEST(SectionedRenderFrame, TransparentCommandsTaggedTransparent)
{
    auto p = make_mixed();
    for (auto& cmd : p.frame.frame.transparent)
        EXPECT_EQ(cmd.stage, PipelineStage::TransparentGeometry);
}

TEST(SectionedRenderFrame, ParticleCommandsTaggedParticle)
{
    auto p = make_mixed();
    for (auto& cmd : p.frame.frame.particles)
        EXPECT_EQ(cmd.stage, PipelineStage::Particle);
}


// ─── Per-cloud splat command fields ──────────────────────────────────────────

TEST(SectionedRenderFrame, SplatCommandsHaveCloudHandleAndFullCloudSemantics)
{
    // Each splat node has a distinct cloud handle (0 and 1).
    // Each DrawCommand should carry that handle with full-cloud draw semantics:
    // splat_instance_count=0 and empty sorted_splat_indices (backend draws all).
    auto p = make_mixed(camera_at_z(0.f));
    for (auto& cmd : p.frame.frame.splats) {
        EXPECT_NE(cmd.splats_buffer, INVALID_SPLAT)      << "expected cloud path command";
        EXPECT_EQ(cmd.kind, DrawCommandKind::GaussianSplats);
        EXPECT_EQ(cmd.splat_instance_count, 0u)           << "full-cloud draw: instance count = 0";
        EXPECT_TRUE(cmd.sorted_splat_indices.empty())     << "full-cloud draw: no sorted indices";
    }
}

TEST(SectionedRenderFrame, MixedLegacyAndCloudSplatsOnlyEmitValidCloudCommands)
{
    SceneBuilder b;

    auto make_splat_node = [](float z) {
        TransformNode n{};
        n.local = translation_z(z);
        n.flags = TransformNodeFlag::RenderDomain;
        return n;
    };

    TransformNode root{};
    auto root_h = add_node(b, root);
    auto legacy = add_node(b, make_splat_node(2.f));
    auto cloud0 = add_node(b, make_splat_node(4.f));
    auto cloud1 = add_node(b, make_splat_node(6.f));

    for (auto n : { legacy, cloud0, cloud1 })
        add_edge(b, root_h, n);

    auto s = build(std::move(b));
    assert(s.has_value());
    propagate_all(s->polytree);

    constexpr SplatHandle cloud_a = 7u;
    constexpr SplatHandle cloud_b = 8u;

    std::vector<RenderableDescriptor> descs(node_count(s->polytree));
    descs[root_h] = { classify_legacy_renderable(RenderPipeline::None) };
    descs[legacy] = { classify_legacy_renderable(RenderPipeline::Splat),
                      INVALID_MESH, INVALID_MATERIAL, {},
                      SplatDescriptor{{1,1,1},{0,0,0,1},{1,1,1},1.f, INVALID_SPLAT, 0u} };
    descs[cloud0] = { classify_legacy_renderable(RenderPipeline::Splat),
                      INVALID_MESH, INVALID_MATERIAL, {},
                      SplatDescriptor{{1,1,1},{0,0,0,1},{1,1,1},1.f, cloud_a, 0u} };
    descs[cloud1] = { classify_legacy_renderable(RenderPipeline::Splat),
                      INVALID_MESH, INVALID_MATERIAL, {},
                      SplatDescriptor{{1,1,1},{0,0,0,1},{1,1,1},1.f, cloud_b, 0u} };

    CompiledSceneStorage compiled{};
    RenderIRStorage      ir_st{};
    RenderFrameStorage   fr_st{};

    compile(compiled, s->polytree, descs, {}, camera_at_z(0.f));
    build_render_ir(ir_st, compiled.scene);
    ASSERT_EQ(ir_st.ir.splats.size(), 3u) << "IR should still see all visible splat primitives";

    build_frame(fr_st, ir_st.ir, compiled.scene);
    ASSERT_EQ(fr_st.frame.splats.size(), 2u)
        << "legacy splats must not become invalid GaussianSplats commands in cloud mode";

    uint32_t cloud_a_count = 0, cloud_b_count = 0;
    for (auto& cmd : fr_st.frame.splats) {
        EXPECT_EQ(cmd.kind, DrawCommandKind::GaussianSplats);
        EXPECT_NE(cmd.splats_buffer, INVALID_SPLAT);
        EXPECT_EQ(cmd.splat_instance_count, 0u);
        EXPECT_TRUE(cmd.sorted_splat_indices.empty());
        if (cmd.splats_buffer == cloud_a) ++cloud_a_count;
        if (cmd.splats_buffer == cloud_b) ++cloud_b_count;
    }

    EXPECT_EQ(cloud_a_count, 1u);
    EXPECT_EQ(cloud_b_count, 1u);

    update_view(compiled, camera_at_z(10.f));
    update_render_ir(ir_st, compiled.scene);
    update_frame_view(fr_st, ir_st.ir, compiled.scene);

    ASSERT_EQ(fr_st.frame.splats.size(), 2u)
        << "update_frame_view must preserve the valid-cloud-only contract";
    for (auto& cmd : fr_st.frame.splats) {
        EXPECT_EQ(cmd.kind, DrawCommandKind::GaussianSplats);
        EXPECT_NE(cmd.splats_buffer, INVALID_SPLAT);
        EXPECT_EQ(cmd.splat_instance_count, 0u);
        EXPECT_TRUE(cmd.sorted_splat_indices.empty());
    }
}

TEST(SectionedRenderFrame, UpdateFrameViewPreservesSplatWorldMatrices)
{
    // After a camera move, each cloud-instance DrawCommand must still carry
    // a non-zero world matrix built from the compiled primitive's position.
    auto p = make_mixed(camera_at_z(0.f));

    update_view(p.compiled, camera_at_z(10.f));
    update_render_ir(p.ir, p.compiled.scene);
    update_frame_view(p.frame, p.ir.ir, p.compiled.scene);

    for (auto& cmd : p.frame.frame.splats) {
        // world must be a valid matrix (identity + translation), not zero-init.
        EXPECT_FLOAT_EQ(cmd.world.m[0],  1.f) << "world[0,0] must be 1 (identity diagonal)";
        EXPECT_FLOAT_EQ(cmd.world.m[15], 1.f) << "world[3,3] must be 1 (identity diagonal)";
    }
}


// ─── Sort order preserved ─────────────────────────────────────────────────────

TEST(SectionedRenderFrame, OpaqueCommandsSortedByMaterial)
{
    auto p = make_mixed();
    const auto& op = p.frame.frame.opaque;
    for (uint32_t i = 1; i < op.size(); ++i)
        EXPECT_LE(op[i-1].sort_key, op[i].sort_key) << "opaque sort broken at " << i;
}

TEST(SectionedRenderFrame, SplatCommandsSortedBackToFront)
{
    auto p = make_mixed(camera_at_z(0.f));
    const auto& sp = p.frame.frame.splats;
    for (uint32_t i = 1; i < sp.size(); ++i)
        EXPECT_LE(sp[i-1].sort_key, sp[i].sort_key) << "splat sort broken at " << i;
}

TEST(SectionedRenderFrame, TransparentCommandsSortedBackToFront)
{
    auto p = make_mixed(camera_at_z(0.f));
    const auto& tr = p.frame.frame.transparent;
    for (uint32_t i = 1; i < tr.size(); ++i)
        EXPECT_LE(tr[i-1].sort_key, tr[i].sort_key) << "transparent sort broken at " << i;
}


// ─── update_frame_view() — opaque section stability ──────────────────────────

TEST(SectionedRenderFrame, UpdateFrameViewPreservesStableBufferPointer)
{
    auto p = make_mixed(camera_at_z(0.f));

    const std::byte* stable_ptr = p.frame.stable_buffer.get();

    // Simulate camera move: update depths, re-sort IR, rebuild view sections only.
    update_view(p.compiled, camera_at_z(5.f));
    update_render_ir(p.ir, p.compiled.scene);
    update_frame_view(p.frame, p.ir.ir, p.compiled.scene);

    EXPECT_EQ(p.frame.stable_buffer.get(), stable_ptr)
        << "update_frame_view reallocated stable_buffer";
}

TEST(SectionedRenderFrame, UpdateFrameViewPreservesOpaqueSpanPointer)
{
    auto p = make_mixed(camera_at_z(0.f));

    const DrawCommand* opaque_data = p.frame.frame.opaque.data();

    update_view(p.compiled, camera_at_z(5.f));
    update_render_ir(p.ir, p.compiled.scene);
    update_frame_view(p.frame, p.ir.ir, p.compiled.scene);

    EXPECT_EQ(p.frame.frame.opaque.data(), opaque_data)
        << "frame.opaque span moved after update_frame_view";
}

TEST(SectionedRenderFrame, UpdateFrameViewPreservesOpaqueContent)
{
    auto p = make_mixed(camera_at_z(0.f));

    // Snapshot opaque world matrices before camera move.
    std::vector<float> world_z_before;
    for (auto& cmd : p.frame.frame.opaque)
        world_z_before.push_back(cmd.world.m[14]);

    update_view(p.compiled, camera_at_z(5.f));
    update_render_ir(p.ir, p.compiled.scene);
    update_frame_view(p.frame, p.ir.ir, p.compiled.scene);

    for (uint32_t i = 0; i < static_cast<uint32_t>(p.frame.frame.opaque.size()); ++i)
        EXPECT_FLOAT_EQ(p.frame.frame.opaque[i].world.m[14], world_z_before[i])
            << "opaque[" << i << "] world changed after update_frame_view";
}

TEST(SectionedRenderFrame, UpdateFrameViewPreservesOpaqueCount)
{
    auto p = make_mixed(camera_at_z(0.f));
    const size_t count_before = p.frame.frame.opaque.size();

    update_view(p.compiled, camera_at_z(5.f));
    update_render_ir(p.ir, p.compiled.scene);
    update_frame_view(p.frame, p.ir.ir, p.compiled.scene);

    EXPECT_EQ(p.frame.frame.opaque.size(), count_before);
}


// ─── update_frame_view() — view-dependent sections are rebuilt ────────────────

TEST(SectionedRenderFrame, UpdateFrameViewUpdatesSplatSortKeys)
{
    // Splat nodes use cloud handles (per-cloud path).
    // Depth is no longer stored in DrawCommand; verify via sort_key recomputation.
    auto p = make_mixed(camera_at_z(0.f));

    // Record sort keys before camera move.
    std::vector<uint64_t> keys_before;
    for (auto& cmd : p.frame.frame.splats)
        keys_before.push_back(cmd.sort_key);

    // Move camera significantly so all depths change.
    update_view(p.compiled, camera_at_z(10.f));
    update_render_ir(p.ir, p.compiled.scene);
    update_frame_view(p.frame, p.ir.ir, p.compiled.scene);

    // At least one splat sort_key must have changed.
    bool any_changed = false;
    for (uint32_t i = 0; i < static_cast<uint32_t>(p.frame.frame.splats.size()); ++i) {
        if (p.frame.frame.splats[i].sort_key != keys_before[i])
            any_changed = true;
    }
    EXPECT_TRUE(any_changed) << "no splat sort_key changed after camera move";
}

TEST(SectionedRenderFrame, UpdateFrameViewUpdatesTransparentDepths)
{
    auto p = make_mixed(camera_at_z(0.f));

    std::vector<float> depth_before;
    // Reconstruct depth from sort_key (not directly stored in DrawCommand for transparent)
    // — instead just verify sort order changed, or check sort_keys changed.
    std::vector<uint64_t> keys_before;
    for (auto& cmd : p.frame.frame.transparent)
        keys_before.push_back(cmd.sort_key);

    update_view(p.compiled, camera_at_z(10.f));
    update_render_ir(p.ir, p.compiled.scene);
    update_frame_view(p.frame, p.ir.ir, p.compiled.scene);

    // Sort keys must have been recomputed — at least one should differ.
    bool any_changed = false;
    for (uint32_t i = 0; i < static_cast<uint32_t>(p.frame.frame.transparent.size()); ++i) {
        if (p.frame.frame.transparent[i].sort_key != keys_before[i])
            any_changed = true;
    }
    EXPECT_TRUE(any_changed) << "no transparent sort_key changed after camera move";
}


// ─── update_frame_view() — particle section is rebuilt ───────────────────────

TEST(SectionedRenderFrame, UpdateFrameViewUpdatesParticleSection)
{
    auto p = make_mixed(camera_at_z(0.f));

    // Capture particle sort keys before camera move.
    std::vector<uint64_t> keys_before;
    for (auto& cmd : p.frame.frame.particles)
        keys_before.push_back(cmd.sort_key);

    // Move camera significantly.
    update_view(p.compiled, camera_at_z(10.f));
    update_render_ir(p.ir, p.compiled.scene);
    update_frame_view(p.frame, p.ir.ir, p.compiled.scene);

    // At least one particle sort_key must have changed.
    bool any_changed = false;
    for (uint32_t i = 0; i < static_cast<uint32_t>(p.frame.frame.particles.size()); ++i) {
        if (p.frame.frame.particles[i].sort_key != keys_before[i])
            any_changed = true;
    }
    EXPECT_TRUE(any_changed) << "no particle sort_key changed after camera move";
}


// ─── build_frame() full rebuild picks up current camera ──────────────────────
//
// Verifies that build_frame() is a complete rebuild: after a camera move,
// calling build_frame() (not update_frame_view()) produces view-dependent
// sections that reflect the new camera, not the camera at the previous build.

TEST(SectionedRenderFrame, BuildFrameFullyRebuildsViewSections)
{
    auto p = make_mixed(camera_at_z(0.f));

    // Capture splat sort keys at z=0.
    std::vector<uint64_t> keys_before;
    for (auto& cmd : p.frame.frame.splats)
        keys_before.push_back(cmd.sort_key);

    // Move camera, re-sort IR, then do a full build_frame rebuild.
    update_view(p.compiled, camera_at_z(10.f));
    update_render_ir(p.ir, p.compiled.scene);
    build_frame(p.frame, p.ir.ir, p.compiled.scene);

    // View-dependent sections must reflect the new camera.
    bool any_changed = false;
    for (uint32_t i = 0; i < static_cast<uint32_t>(p.frame.frame.splats.size()); ++i) {
        if (p.frame.frame.splats[i].sort_key != keys_before[i])
            any_changed = true;
    }
    EXPECT_TRUE(any_changed) << "build_frame() did not update splat section after camera move";
}

TEST(SectionedRenderFrame, UpdateFrameViewMatchesFreshBuildAfterCameraMove)
{
    auto p = make_mixed(camera_at_z(0.f));

    update_view(p.compiled, camera_at_z(10.f));
    update_render_ir(p.ir, p.compiled.scene);
    auto updated = update_frame_view(p.frame, p.ir.ir, p.compiled.scene);

    RenderFrameStorage fresh_storage{};
    auto fresh = build_frame(fresh_storage, p.ir.ir, p.compiled.scene);

    expect_same_frame_sections(updated, fresh);
}


// ─── Stable buffer not reallocated by repeated update_frame_view() calls ─────

TEST(SectionedRenderFrame, RepeatedUpdateFrameViewDoesNotReallocate)
{
    auto p = make_mixed(camera_at_z(0.f));

    const std::byte* stable_ptr = p.frame.stable_buffer.get();
    const std::byte* view_ptr   = p.frame.view_buffer.get();

    for (int i = 1; i <= 5; ++i) {
        update_view(p.compiled, camera_at_z(static_cast<float>(i)));
        update_render_ir(p.ir, p.compiled.scene);
        update_frame_view(p.frame, p.ir.ir, p.compiled.scene);
    }

    EXPECT_EQ(p.frame.stable_buffer.get(), stable_ptr) << "stable_buffer reallocated";
    EXPECT_EQ(p.frame.view_buffer.get(),   view_ptr)   << "view_buffer reallocated";
}


// ─── Regression: previously culled opaque appears in frame after camera move ──
//
// Regression for the bug where update_frame_view() left frame.opaque frozen at
// the initial build_frame() state, so newly visible opaques were never emitted
// as DrawCommands regardless of frustum changes.
//
// Scene: A (material=1) at z=0, B (material=2) at z=5.
// Initial frustum (identity VP): unit-cube [-1,1]^3 — A visible, B culled.
// After camera move to z=5 (frustum [4,6]): B visible, A culled.

TEST(SectionedRenderFrame, CameraMoveRevealsCulledOpaqueInFrame)
{
    ViewData narrow{};
    narrow.view            = Mat4::identity();
    narrow.projection      = Mat4::identity();
    narrow.view_projection = Mat4::identity(); // unit-cube frustum

    SceneBuilder b;
    TransformNode root{}; root.local = Mat4::identity();
    NodeHandle root_h = add_node(b, root);

    TransformNode na{}; na.local = translation_z(0.f); na.flags = TransformNodeFlag::RenderDomain;
    TransformNode nb{}; nb.local = translation_z(5.f); nb.flags = TransformNodeFlag::RenderDomain;
    NodeHandle ha = add_node(b, na);
    NodeHandle hb = add_node(b, nb);
    add_edge(b, root_h, ha);
    add_edge(b, root_h, hb);

    auto s = build(std::move(b));
    assert(s.has_value());
    propagate_all(s->polytree);

    std::vector<RenderableDescriptor> descs(node_count(s->polytree));
    descs[root_h] = { classify_legacy_renderable(RenderPipeline::None) };
    descs[ha]     = { classify_legacy_renderable(RenderPipeline::OpaqueGeometry), 1u, 1u, unit_box() };
    descs[hb]     = { classify_legacy_renderable(RenderPipeline::OpaqueGeometry), 2u, 2u, unit_box() };

    CompiledSceneStorage compiled{};
    RenderIRStorage      ir_st{};
    RenderFrameStorage   fr_st{};

    compile(compiled, s->polytree, descs, {}, narrow);
    build_render_ir(ir_st, compiled.scene);
    build_frame(fr_st, ir_st.ir, compiled.scene);

    ASSERT_EQ(fr_st.frame.opaque.size(), 1u) << "only A should be visible initially";
    EXPECT_EQ(fr_st.frame.opaque[0].material, 1u) << "A (material=1) should be the initial visible one";

    // Move camera to z=5: frustum shifts to [4,6] — B visible, A culled.
    ViewData moved{};
    moved.view               = Mat4::identity();
    moved.view.m[14]         = -5.f;
    moved.projection         = Mat4::identity();
    moved.view_projection    = Mat4::identity();
    moved.view_projection.m[14] = -5.f;
    moved.camera_position    = Vec3{ 0.f, 0.f, 5.f };

    update_view(compiled, moved);
    update_render_ir(ir_st, compiled.scene);
    update_frame_view(fr_st, ir_st.ir, compiled.scene);

    ASSERT_EQ(fr_st.frame.opaque.size(), 1u) << "exactly one opaque visible after camera move";
    EXPECT_EQ(fr_st.frame.opaque[0].material, 2u)
        << "B (material=2) must appear in frame after camera move reveals it";
}

TEST(SectionedRenderFrame, UpdateFrameViewCanGrowOpaqueFromZeroVisible)
{
    ViewData narrow{};
    narrow.view            = Mat4::identity();
    narrow.projection      = Mat4::identity();
    narrow.view_projection = Mat4::identity();

    SceneBuilder b;
    TransformNode root{};
    auto root_h = add_node(b, root);

    TransformNode far_node{};
    far_node.local = translation_z(5.f);
    far_node.flags = TransformNodeFlag::RenderDomain;
    auto far_h = add_node(b, far_node);
    add_edge(b, root_h, far_h);

    auto s = build(std::move(b));
    assert(s.has_value());
    propagate_all(s->polytree);

    std::vector<RenderableDescriptor> descs(node_count(s->polytree));
    descs[root_h] = { classify_legacy_renderable(RenderPipeline::None) };
    descs[far_h]  = { classify_legacy_renderable(RenderPipeline::OpaqueGeometry), 9u, 42u, unit_box() };

    CompiledSceneStorage compiled{};
    RenderIRStorage      ir_st{};
    RenderFrameStorage   fr_st{};

    compile(compiled, s->polytree, descs, {}, narrow);
    build_render_ir(ir_st, compiled.scene);
    build_frame(fr_st, ir_st.ir, compiled.scene);

    ASSERT_EQ(fr_st.frame.opaque.size(), 0u) << "far object should start fully culled";

    ViewData moved{};
    moved.view                  = Mat4::identity();
    moved.view.m[14]            = -5.f;
    moved.projection            = Mat4::identity();
    moved.view_projection       = Mat4::identity();
    moved.view_projection.m[14] = -5.f;
    moved.camera_position       = Vec3{ 0.f, 0.f, 5.f };

    update_view(compiled, moved);
    update_render_ir(ir_st, compiled.scene);
    update_frame_view(fr_st, ir_st.ir, compiled.scene);

    ASSERT_EQ(fr_st.frame.opaque.size(), 1u)
        << "update_frame_view must grow opaque span from zero visible commands";
    EXPECT_EQ(fr_st.frame.opaque[0].mesh, 9u);
    EXPECT_EQ(fr_st.frame.opaque[0].material, 42u);
}


// ─── Cloud-instance DrawCommands: same cloud, different transforms ────────────
//
// Two scene nodes reference the same cloud_handle but have different world
// positions.  Each must produce its own DrawCommand with the correct world
// matrix — same cloud resource, different transforms, separate draw calls.
//
// This is the normal render case for instanced splat clouds: many nodes can
// reference the same cloud GPU resource but each node has its own transform.

TEST(SectionedRenderFrame, SameCloudDifferentTransformsProduceSeparateCommands)
{
    SceneBuilder b;

    auto make_splat_node = [](float z) {
        TransformNode n{};
        n.local = translation_z(z);
        n.flags = TransformNodeFlag::RenderDomain;
        return n;
    };

    TransformNode root{};
    auto root_h = add_node(b, root);

    auto sp0 = add_node(b, make_splat_node(3.f));   // cloud 0, position z=3
    auto sp1 = add_node(b, make_splat_node(7.f));   // cloud 0, position z=7

    add_edge(b, root_h, sp0);
    add_edge(b, root_h, sp1);

    auto s = build(std::move(b));
    assert(s.has_value());
    propagate_all(s->polytree);

    const SplatHandle same_cloud = 0u;

    std::vector<RenderableDescriptor> descs(node_count(s->polytree));
    descs[root_h] = { classify_legacy_renderable(RenderPipeline::None) };
    descs[sp0] = { classify_legacy_renderable(RenderPipeline::Splat),
                   INVALID_MESH, INVALID_MATERIAL, {},
                   SplatDescriptor{{1,1,1},{0,0,0,1},{1,1,1},1.f, same_cloud, 0u} };
    descs[sp1] = { classify_legacy_renderable(RenderPipeline::Splat),
                   INVALID_MESH, INVALID_MATERIAL, {},
                   SplatDescriptor{{1,1,1},{0,0,0,1},{1,1,1},1.f, same_cloud, 0u} };

    CompiledSceneStorage compiled{};
    RenderIRStorage      ir_st{};
    RenderFrameStorage   fr_st{};

    compile(compiled, s->polytree, descs, {}, camera_at_z(0.f));
    build_render_ir(ir_st, compiled.scene);
    build_frame(fr_st, ir_st.ir, compiled.scene);

    const auto& splats = fr_st.frame.splats;
    ASSERT_EQ(splats.size(), 2u) << "one DrawCommand per cloud instance";

    // Both reference the same cloud resource.
    EXPECT_EQ(splats[0].splats_buffer, same_cloud);
    EXPECT_EQ(splats[1].splats_buffer, same_cloud);

    // World matrices must differ (different scene positions).
    EXPECT_NE(splats[0].world.m[14], splats[1].world.m[14])
        << "cloud instances at different z must have different world matrices";

    // Neither world matrix may be zero.
    EXPECT_FLOAT_EQ(splats[0].world.m[0],  1.f) << "world diagonal must be identity";
    EXPECT_FLOAT_EQ(splats[0].world.m[15], 1.f) << "world diagonal must be identity";
    EXPECT_FLOAT_EQ(splats[1].world.m[0],  1.f) << "world diagonal must be identity";
    EXPECT_FLOAT_EQ(splats[1].world.m[15], 1.f) << "world diagonal must be identity";

    // Full-cloud draw semantics: backend draws all splats in the resource.
    EXPECT_EQ(splats[0].splat_instance_count, 0u);
    EXPECT_EQ(splats[1].splat_instance_count, 0u);
    EXPECT_TRUE(splats[0].sorted_splat_indices.empty());
    EXPECT_TRUE(splats[1].sorted_splat_indices.empty());
}


// ─── Cloud-instance per-primitive DrawCommands for multiple clouds ────────────
//
// Five scene nodes interleaving two different cloud handles.
// Each node instances a full cloud at a given position.
// Verifies: 5 DrawCommands, each with correct cloud handle and position in world.

TEST(SectionedRenderFrame, MultipleCloudInstancesProducePerPrimitiveCommands)
{
    SceneBuilder b;

    auto make_splat_node = [](float z) {
        TransformNode n{};
        n.local = translation_z(z);
        n.flags = TransformNodeFlag::RenderDomain;
        return n;
    };

    TransformNode root{};
    auto root_h = add_node(b, root);

    auto sp0 = add_node(b, make_splat_node(10.f));
    auto sp1 = add_node(b, make_splat_node( 8.f));
    auto sp2 = add_node(b, make_splat_node( 7.f));
    auto sp3 = add_node(b, make_splat_node( 3.f));
    auto sp4 = add_node(b, make_splat_node( 1.f));

    for (auto n : { sp0, sp1, sp2, sp3, sp4 })
        add_edge(b, root_h, n);

    auto s = build(std::move(b));
    assert(s.has_value());
    propagate_all(s->polytree);

    std::vector<RenderableDescriptor> descs(node_count(s->polytree));
    descs[root_h] = { classify_legacy_renderable(RenderPipeline::None) };
    descs[sp0] = { classify_legacy_renderable(RenderPipeline::Splat),
                   INVALID_MESH, INVALID_MATERIAL, {},
                   SplatDescriptor{{1,1,1},{0,0,0,1},{1,1,1},1.f, 0u, 0u} };
    descs[sp1] = { classify_legacy_renderable(RenderPipeline::Splat),
                   INVALID_MESH, INVALID_MATERIAL, {},
                   SplatDescriptor{{1,1,1},{0,0,0,1},{1,1,1},1.f, 1u, 0u} };
    descs[sp2] = { classify_legacy_renderable(RenderPipeline::Splat),
                   INVALID_MESH, INVALID_MATERIAL, {},
                   SplatDescriptor{{1,1,1},{0,0,0,1},{1,1,1},1.f, 0u, 0u} };
    descs[sp3] = { classify_legacy_renderable(RenderPipeline::Splat),
                   INVALID_MESH, INVALID_MATERIAL, {},
                   SplatDescriptor{{1,1,1},{0,0,0,1},{1,1,1},1.f, 0u, 0u} };
    descs[sp4] = { classify_legacy_renderable(RenderPipeline::Splat),
                   INVALID_MESH, INVALID_MATERIAL, {},
                   SplatDescriptor{{1,1,1},{0,0,0,1},{1,1,1},1.f, 1u, 0u} };

    CompiledSceneStorage compiled{};
    RenderIRStorage      ir_st{};
    RenderFrameStorage   fr_st{};

    compile(compiled, s->polytree, descs, {}, camera_at_z(0.f));
    build_render_ir(ir_st, compiled.scene);
    build_frame(fr_st, ir_st.ir, compiled.scene);

    const auto& splats = fr_st.frame.splats;
    ASSERT_EQ(splats.size(), 5u) << "one DrawCommand per cloud-instance scene node";

    // Every command must have valid cloud handle and non-zero world.
    for (auto& cmd : splats) {
        EXPECT_NE(cmd.splats_buffer, INVALID_SPLAT);
        EXPECT_EQ(cmd.kind, DrawCommandKind::GaussianSplats);
        EXPECT_FLOAT_EQ(cmd.world.m[15], 1.f) << "world must not be zero-init";
        EXPECT_EQ(cmd.splat_instance_count, 0u) << "full-cloud draw";
        EXPECT_TRUE(cmd.sorted_splat_indices.empty()) << "full-cloud draw";
    }

    // Count commands per cloud handle.
    uint32_t cloud0_count = 0, cloud1_count = 0;
    for (auto& cmd : splats) {
        if (cmd.splats_buffer == 0u) ++cloud0_count;
        if (cmd.splats_buffer == 1u) ++cloud1_count;
    }
    EXPECT_EQ(cloud0_count, 3u) << "cloud 0 has 3 instances";
    EXPECT_EQ(cloud1_count, 2u) << "cloud 1 has 2 instances";
}
