#include <gtest/gtest.h>
#include <scene/compile/scene_compiler.h>
#include <scene/compile/legacy_classification.h>

using namespace wz::scene;
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

    Mat4 translation(float x, float y, float z)
    {
        Mat4 m = Mat4::identity();
        m.m[12] = x; m.m[13] = y; m.m[14] = z;
        return m;
    }

    struct TwoNodeScene
    {
        SceneStorage                      storage{};
        std::vector<RenderableDescriptor> descs{};
        NodeHandle                        root{};
        NodeHandle                        obj{};
    };

    TwoNodeScene make_scene(SceneNodeClass node_class,
                            MeshHandle mesh       = 0u,
                            MaterialHandle mat    = 0u)
    {
        SceneBuilder b;

        TransformNode root_node{};
        root_node.local = Mat4::identity();
        NodeHandle root_h = add_node(b, root_node);

        TransformNode obj_node{};
        obj_node.local = Mat4::identity();
        obj_node.flags = TransformNodeFlag::RenderDomain;
        NodeHandle obj_h = add_node(b, obj_node);

        EXPECT_TRUE(add_edge(b, root_h, obj_h));
        auto result = build(std::move(b));
        EXPECT_TRUE(result.has_value());

        TwoNodeScene out{};
        out.storage = std::move(*result);
        out.root    = root_h;
        out.obj     = obj_h;

        out.descs.resize(node_count(out.storage.polytree));
        out.descs[root_h] = RenderableDescriptor{
            .node_class = classify_legacy_renderable(RenderPipeline::None),
        };
        out.descs[obj_h] = RenderableDescriptor{
            .node_class   = node_class,
            .mesh         = mesh,
            .material     = mat,
            .local_bounds = unit_box(),
            .visible      = true,
        };

        propagate_all(out.storage.polytree);
        return out;
    }

} // namespace


// ─── Material dirty patches opaque mesh and material handles ─────────────────

TEST(DescriptorUpdate, MaterialDirtyPatchesOpaqueHandles)
{
    auto scene = make_scene(
        classify_legacy_renderable(RenderPipeline::OpaqueGeometry), 1u, 10u);
    CompiledSceneStorage storage{};
    compile(storage, scene.storage.polytree, scene.descs, {}, identity_view());

    ASSERT_EQ(storage.scene.opaque.size(), 1u);
    EXPECT_EQ(storage.scene.opaque[0].mesh,     1u);
    EXPECT_EQ(storage.scene.opaque[0].material, 10u);

    // Swap mesh and material handles.
    scene.descs[scene.obj].mesh     = 99u;
    scene.descs[scene.obj].material = 77u;

    refresh_compiled_scene(
        storage, scene.storage.polytree, scene.descs, {},
        identity_view(), SceneDirtyBits::Material);

    EXPECT_EQ(storage.scene.opaque[0].mesh,     99u)
        << "mesh handle not updated by Material dirty";
    EXPECT_EQ(storage.scene.opaque[0].material, 77u)
        << "material handle not updated by Material dirty";
}


// ─── Material dirty patches transparent mesh and material handles ─────────────

TEST(DescriptorUpdate, MaterialDirtyPatchesTransparentHandles)
{
    auto scene = make_scene(
        classify_legacy_renderable(RenderPipeline::TransparentGeometry), 2u, 20u);
    CompiledSceneStorage storage{};
    compile(storage, scene.storage.polytree, scene.descs, {}, identity_view());

    ASSERT_EQ(storage.scene.transparent.size(), 1u);
    EXPECT_EQ(storage.scene.transparent[0].mesh,     2u);
    EXPECT_EQ(storage.scene.transparent[0].material, 20u);

    scene.descs[scene.obj].mesh     = 55u;
    scene.descs[scene.obj].material = 44u;

    refresh_compiled_scene(
        storage, scene.storage.polytree, scene.descs, {},
        identity_view(), SceneDirtyBits::Material);

    EXPECT_EQ(storage.scene.transparent[0].mesh,     55u)
        << "transparent mesh handle not updated";
    EXPECT_EQ(storage.scene.transparent[0].material, 44u)
        << "transparent material handle not updated";
}


// ─── Material dirty patches splat appearance data ────────────────────────────

TEST(DescriptorUpdate, MaterialDirtyPatchesSplatAppearance)
{
    auto scene = make_scene(classify_legacy_renderable(RenderPipeline::Splat));
    scene.descs[scene.obj].splat_data = SplatDescriptor{
        .scale    = { 1.f, 1.f, 1.f },
        .rotation = { 0.f, 0.f, 0.f, 1.f },
        .color    = { 1.f, 0.f, 0.f },
        .opacity  = 0.5f,
    };

    CompiledSceneStorage storage{};
    compile(storage, scene.storage.polytree, scene.descs, {}, identity_view());

    ASSERT_EQ(storage.scene.splats.size(), 1u);
    EXPECT_FLOAT_EQ(storage.scene.splats[0].opacity, 0.5f);

    scene.descs[scene.obj].splat_data.opacity  = 0.9f;
    scene.descs[scene.obj].splat_data.color    = { 0.f, 1.f, 0.f };

    refresh_compiled_scene(
        storage, scene.storage.polytree, scene.descs, {},
        identity_view(), SceneDirtyBits::Material);

    EXPECT_FLOAT_EQ(storage.scene.splats[0].opacity,   0.9f)
        << "splat opacity not updated";
    EXPECT_FLOAT_EQ(storage.scene.splats[0].color.y,   1.f)
        << "splat color not updated";
}


// ─── Material dirty does not reallocate buffers ───────────────────────────────

TEST(DescriptorUpdate, MaterialDirtyDoesNotReallocate)
{
    auto scene = make_scene(
        classify_legacy_renderable(RenderPipeline::OpaqueGeometry), 1u, 1u);
    CompiledSceneStorage storage{};
    compile(storage, scene.storage.polytree, scene.descs, {}, identity_view());

    const std::byte* stable_ptr   = storage.stable_buffer.get();
    const std::byte* meta_ptr     = storage.metadata_buffer.get();
    const std::byte* view_ptr     = storage.view_buffer.get();

    scene.descs[scene.obj].mesh = 42u;

    refresh_compiled_scene(
        storage, scene.storage.polytree, scene.descs, {},
        identity_view(), SceneDirtyBits::Material);

    EXPECT_EQ(storage.stable_buffer.get(),    stable_ptr) << "stable_buffer reallocated";
    EXPECT_EQ(storage.metadata_buffer.get(),  meta_ptr)   << "metadata_buffer reallocated";
    EXPECT_EQ(storage.view_buffer.get(),      view_ptr)   << "view_buffer reallocated";
}


// ─── Material dirty does not change world matrix ─────────────────────────────

TEST(DescriptorUpdate, MaterialDirtyDoesNotChangeWorldMatrix)
{
    auto scene = make_scene(
        classify_legacy_renderable(RenderPipeline::OpaqueGeometry), 1u, 1u);
    CompiledSceneStorage storage{};
    compile(storage, scene.storage.polytree, scene.descs, {}, identity_view());

    const float world_z_before = storage.scene.opaque[0].world.m[14];

    scene.descs[scene.obj].mesh = 7u;

    refresh_compiled_scene(
        storage, scene.storage.polytree, scene.descs, {},
        identity_view(), SceneDirtyBits::Material);

    EXPECT_FLOAT_EQ(storage.scene.opaque[0].world.m[14], world_z_before)
        << "world matrix changed on Material-only update";
}


// ─── Transform + Material dirty updates both world matrix and handles ─────────
//
// Proves the combined path: update_compiled_transforms() runs (world matrix
// updated), then update_compiled_descriptors() runs (mesh handle updated).

TEST(DescriptorUpdate, TransformAndMaterialDirtyUpdatesBoth)
{
    auto scene = make_scene(
        classify_legacy_renderable(RenderPipeline::OpaqueGeometry), 5u, 5u);
    CompiledSceneStorage storage{};
    compile(storage, scene.storage.polytree, scene.descs, {}, identity_view());

    ASSERT_EQ(storage.scene.opaque.size(), 1u);
    EXPECT_EQ(storage.scene.opaque[0].mesh, 5u);
    EXPECT_FLOAT_EQ(storage.scene.opaque[0].world.m[14], 0.f);

    // Move the object in world space and change its mesh handle.
    const_cast<TransformNode&>(node_data(scene.storage.polytree, scene.obj)).world
        = translation(0.f, 0.f, 8.f);
    scene.descs[scene.obj].mesh = 99u;

    refresh_compiled_scene(
        storage, scene.storage.polytree, scene.descs, {},
        identity_view(),
        SceneDirtyBits::Transform | SceneDirtyBits::Material);

    EXPECT_FLOAT_EQ(storage.scene.opaque[0].world.m[14], 8.f)
        << "world matrix not updated by Transform dirty";
    EXPECT_EQ(storage.scene.opaque[0].mesh, 99u)
        << "mesh handle not updated by Material dirty";
}


// ─── direct update_compiled_descriptors() call ────────────────────────────────
//
// Verifies the function can be called directly (not just via refresh routing).

TEST(DescriptorUpdate, DirectCallUpdatesHandles)
{
    auto scene = make_scene(
        classify_legacy_renderable(RenderPipeline::OpaqueGeometry), 3u, 3u);
    CompiledSceneStorage storage{};
    compile(storage, scene.storage.polytree, scene.descs, {}, identity_view());

    scene.descs[scene.obj].mesh     = 88u;
    scene.descs[scene.obj].material = 66u;

    update_compiled_descriptors(storage, scene.descs);

    EXPECT_EQ(storage.scene.opaque[0].mesh,     88u);
    EXPECT_EQ(storage.scene.opaque[0].material, 66u);
}
