#include <gtest/gtest.h>
#include <scene/compile/scene_compiler.h>
#include <scene/compile/legacy_classification.h>
#include <array>

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
        v.view_projection = Mat4::identity();
        v.camera_position = Vec3{ 0.f, 0.f, z };
        return v;
    }

    SceneStorage make_scene(std::vector<RenderableDescriptor>& descs_out)
    {
        SceneBuilder b;

        auto make_node = [](float z, uint16_t flags) {
            TransformNode n{};
            n.local = translation_z(z);
            n.flags = flags;
            return n;
            };

        auto root = add_node(b, make_node(0.f, 0));
        auto opaque = add_node(b, make_node(1.f, TransformNodeFlag::RenderDomain));
        auto transp = add_node(b, make_node(2.f, TransformNodeFlag::RenderDomain));
        auto splat = add_node(b, make_node(3.f, TransformNodeFlag::RenderDomain));
        auto particle = add_node(b, make_node(4.f, TransformNodeFlag::RenderDomain));

        add_edge(b, root, opaque);
        add_edge(b, root, transp);
        add_edge(b, root, splat);
        add_edge(b, root, particle);

        auto storage = build(std::move(b));
        assert(storage.has_value());
        propagate_all(storage->polytree);

        descs_out.resize(5);
        descs_out[root]     = { classify_legacy_renderable(RenderPipeline::None) };
        descs_out[opaque]   = { .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
                                .mesh = 0u, .material = 0u,
                                .local_bounds = unit_box(), .visible = true };
        descs_out[transp]   = { .node_class = classify_legacy_renderable(RenderPipeline::TransparentGeometry),
                                .mesh = 1u, .material = 1u,
                                .local_bounds = unit_box(), .visible = true };
        descs_out[splat]    = { .node_class = classify_legacy_renderable(RenderPipeline::Splat),
                                .mesh = INVALID_MESH, .material = INVALID_MATERIAL,
                                .splat_data = SplatDescriptor{{1,2,3},{0,0,0,1},{1,0,0},0.8f},
                                .visible = true };
        descs_out[particle] = { .node_class = classify_legacy_renderable(RenderPipeline::Particle),
                                .mesh = 2u, .material = 2u,
                                .local_bounds = unit_box(), .visible = true };

        return std::move(*storage);
    }

} // namespace


// ─── Stable data — unchanged by update_view() ────────────────────────────────

TEST(SceneCompilerSpec, StableOpaqueCountCorrect)
{
    std::vector<RenderableDescriptor> descs;
    auto scene = make_scene(descs);
    CompiledSceneStorage storage; compile(storage, scene.polytree, descs, {}, camera_at_z(0.f));
    EXPECT_EQ(storage.scene.opaque.size(), 1u);
}

TEST(SceneCompilerSpec, StableSplatCountCorrect)
{
    std::vector<RenderableDescriptor> descs;
    auto scene = make_scene(descs);
    CompiledSceneStorage storage; compile(storage, scene.polytree, descs, {}, camera_at_z(0.f));
    EXPECT_EQ(storage.scene.splats.size(), 1u);
}

TEST(SceneCompilerSpec, OpaqueWorldTransformStable)
{
    std::vector<RenderableDescriptor> descs;
    auto scene = make_scene(descs);
    CompiledSceneStorage storage; compile(storage, scene.polytree, descs, {}, camera_at_z(0.f));

    float z_before = storage.scene.opaque[0].world.m[14];
    update_view(storage, camera_at_z(100.f));
    float z_after = storage.scene.opaque[0].world.m[14];

    EXPECT_FLOAT_EQ(z_before, z_after); // stable — camera move must not change it
}

TEST(SceneCompilerSpec, SplatDataStable)
{
    std::vector<RenderableDescriptor> descs;
    auto scene = make_scene(descs);
    CompiledSceneStorage storage; compile(storage, scene.polytree, descs, {}, camera_at_z(0.f));

    float opacity_before = storage.scene.splats[0].opacity;
    update_view(storage, camera_at_z(100.f));
    float opacity_after = storage.scene.splats[0].opacity;

    EXPECT_FLOAT_EQ(opacity_before, opacity_after);
}

TEST(SceneCompilerSpec, SplatPositionFromWorldTransform)
{
    std::vector<RenderableDescriptor> descs;
    auto scene = make_scene(descs);
    CompiledSceneStorage storage; compile(storage, scene.polytree, descs, {}, camera_at_z(0.f));

    EXPECT_FLOAT_EQ(storage.scene.splats[0].position.z, 3.f);
}

TEST(SceneCompilerSpec, SplatDataPreserved)
{
    std::vector<RenderableDescriptor> descs;
    auto scene = make_scene(descs);
    CompiledSceneStorage storage; compile(storage, scene.polytree, descs, {}, camera_at_z(0.f));
    auto& s = storage.scene.splats[0];

    EXPECT_FLOAT_EQ(s.scale.x, 1.f);
    EXPECT_FLOAT_EQ(s.scale.y, 2.f);
    EXPECT_FLOAT_EQ(s.scale.z, 3.f);
    EXPECT_FLOAT_EQ(s.color.x, 1.f);
    EXPECT_FLOAT_EQ(s.opacity, 0.8f);
}

TEST(SceneCompilerSpec, LightsStable)
{
    std::vector<RenderableDescriptor> descs;
    auto scene = make_scene(descs);

    LightRecord sun{};
    sun.intensity = 2.f;
    sun.type = LightType::Directional;
    std::array<LightRecord, 1> lights{ sun };

    CompiledSceneStorage storage; compile(storage, scene.polytree, descs, lights, camera_at_z(0.f));
    ASSERT_EQ(storage.scene.lights.size(), 1u);

    update_view(storage, camera_at_z(50.f));
    EXPECT_FLOAT_EQ(storage.scene.lights[0].intensity, 2.f); // unchanged
}


// ─── View-dependent data — updated by update_view() ──────────────────────────

TEST(SceneCompilerSpec, SplatDepthsPopulatedByCompile)
{
    std::vector<RenderableDescriptor> descs;
    auto scene = make_scene(descs);
    CompiledSceneStorage storage; compile(storage, scene.polytree, descs, {}, camera_at_z(0.f));

    ASSERT_EQ(storage.scene.splat_depths.size(), 1u);
    EXPECT_NE(storage.scene.splat_depths[0], 0.f);
}

TEST(SceneCompilerSpec, SplatDepthChangesWithCamera)
{
    std::vector<RenderableDescriptor> descs;
    auto scene = make_scene(descs);
    CompiledSceneStorage storage; compile(storage, scene.polytree, descs, {}, camera_at_z(0.f));

    float depth_before = storage.scene.splat_depths[0];
    update_view(storage, camera_at_z(10.f));
    float depth_after = storage.scene.splat_depths[0];

    EXPECT_NE(depth_before, depth_after);
}

TEST(SceneCompilerSpec, TransparentDepthChangesWithCamera)
{
    std::vector<RenderableDescriptor> descs;
    auto scene = make_scene(descs);
    CompiledSceneStorage storage; compile(storage, scene.polytree, descs, {}, camera_at_z(0.f));

    ASSERT_EQ(storage.scene.transparent.size(), 1u);
    float depth_before = storage.scene.transparent[0].depth;
    update_view(storage, camera_at_z(10.f));
    float depth_after = storage.scene.transparent[0].depth;

    EXPECT_NE(depth_before, depth_after);
}

TEST(SceneCompilerSpec, ParticleDepthChangesWithCamera)
{
    std::vector<RenderableDescriptor> descs;
    auto scene = make_scene(descs);
    CompiledSceneStorage storage; compile(storage, scene.polytree, descs, {}, camera_at_z(0.f));

    ASSERT_EQ(storage.scene.particles.size(), 1u);
    float depth_before = storage.scene.particles[0].depth;
    update_view(storage, camera_at_z(10.f));
    float depth_after = storage.scene.particles[0].depth;

    EXPECT_NE(depth_before, depth_after);
}

TEST(SceneCompilerSpec, ViewDataUpdatedByUpdateView)
{
    std::vector<RenderableDescriptor> descs;
    auto scene = make_scene(descs);
    CompiledSceneStorage storage; compile(storage, scene.polytree, descs, {}, camera_at_z(0.f));

    update_view(storage, camera_at_z(99.f));
    EXPECT_FLOAT_EQ(storage.scene.view.camera_position.z, 99.f);
}


// ─── Filtering ────────────────────────────────────────────────────────────────

TEST(SceneCompilerSpec, InvisibleNodeProducesNoPrimitive)
{
    std::vector<RenderableDescriptor> descs;
    auto scene = make_scene(descs);
    descs[1].visible = false;
    CompiledSceneStorage storage; compile(storage, scene.polytree, descs, {}, camera_at_z(0.f));
    EXPECT_EQ(storage.scene.opaque.size(), 0u);
}

TEST(SceneCompilerSpec, InvalidMeshExcluded)
{
    std::vector<RenderableDescriptor> descs;
    auto scene = make_scene(descs);
    descs[1].mesh = INVALID_MESH;
    CompiledSceneStorage storage; compile(storage, scene.polytree, descs, {}, camera_at_z(0.f));
    EXPECT_EQ(storage.scene.opaque.size(), 0u);
}

TEST(SceneCompilerSpec, EmptySceneProducesNoPrimitives)
{
    SceneBuilder b;
    TransformNode root{};
    add_node(b, root);
    auto s = build(std::move(b));
    ASSERT_TRUE(s.has_value());
    propagate_all(s->polytree);

    std::vector<RenderableDescriptor> descs(1);
    descs[0] = { classify_legacy_renderable(RenderPipeline::None) };

    CompiledSceneStorage storage; compile(storage, s->polytree, descs, {}, camera_at_z(0.f));
    EXPECT_EQ(storage.scene.opaque.size(), 0u);
    EXPECT_EQ(storage.scene.splats.size(), 0u);
    EXPECT_EQ(storage.scene.transparent.size(), 0u);
    EXPECT_EQ(storage.scene.particles.size(), 0u);
}