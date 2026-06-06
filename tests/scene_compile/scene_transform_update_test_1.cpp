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

    ViewData camera_at(float z)
    {
        ViewData v{};
        v.view            = Mat4::identity();
        v.view.m[14]      = -z;
        v.projection      = Mat4::identity();
        v.view_projection = Mat4::identity();
        v.camera_position = Vec3{ 0.f, 0.f, z };
        return v;
    }

    Mat4 translation(float x, float y, float z)
    {
        Mat4 m = Mat4::identity();
        m.m[12] = x; m.m[13] = y; m.m[14] = z;
        return m;
    }

    // Set the world matrix of a node directly, simulating that propagation has
    // already run. update_compiled_transforms() assumes world matrices are current.
    void set_world(SceneGraph& g, NodeHandle n, const Mat4& world)
    {
        const_cast<TransformNode&>(node_data(g, n)).world = world;
    }

    struct TwoNodeScene
    {
        SceneStorage                      storage{};
        std::vector<RenderableDescriptor> descs{};
        NodeHandle                        root{};
        NodeHandle                        obj{};
    };

    TwoNodeScene make_scene(SceneNodeClass node_class, MeshHandle mesh = 0u)
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
        out.descs[root_h] = RenderableDescriptor{ .node_class = classify_legacy_renderable(RenderPipeline::None) };
        out.descs[obj_h]  = RenderableDescriptor{
            .node_class   = node_class,
            .mesh         = mesh,
            .material     = 0u,
            .local_bounds = unit_box(),
            .visible      = true,
        };

        propagate_all(out.storage.polytree);
        return out;
    }

} // namespace


// ─── update_compiled_transforms: opaque world and bounds ─────────────────────

TEST(UpdateCompiledTransforms, OpaqueWorldMatrixAndBoundsUpdated)
{
    auto scene = make_scene(classify_legacy_renderable(RenderPipeline::OpaqueGeometry));
    CompiledSceneStorage storage{};
    compile(storage, scene.storage.polytree, scene.descs, {}, camera_at(0.f));

    ASSERT_EQ(storage.scene.opaque.size(), 1u);
    EXPECT_EQ(storage.scene.opaque[0].world.m[14], 0.f);

    // Move the object in world space (propagation already done by caller).
    const Mat4 new_world = translation(0.f, 0.f, 7.f);
    set_world(scene.storage.polytree, scene.obj, new_world);

    update_compiled_transforms(storage, scene.storage.polytree, scene.descs, camera_at(0.f));

    EXPECT_FLOAT_EQ(storage.scene.opaque[0].world.m[14], 7.f)
        << "world matrix not updated";

    // Bounds should reflect the new world position.
    // unit_box at z=7 → world AABB min.z ≈ 6.5, max.z ≈ 7.5
    EXPECT_NEAR(storage.scene.opaque[0].bounds.min.z, 6.5f, 1e-4f)
        << "bounds min.z not updated";
    EXPECT_NEAR(storage.scene.opaque[0].bounds.max.z, 7.5f, 1e-4f)
        << "bounds max.z not updated";
}


// ─── update_compiled_transforms: splat position ──────────────────────────────

TEST(UpdateCompiledTransforms, SplatPositionExtractedFromWorldTranslation)
{
    auto scene = make_scene(classify_legacy_renderable(RenderPipeline::Splat));
    scene.descs[scene.obj].splat_data = SplatDescriptor{
        .scale    = { 1.f, 1.f, 1.f },
        .rotation = { 0.f, 0.f, 0.f, 1.f },
        .color    = { 1.f, 1.f, 1.f },
        .opacity  = 1.f,
    };

    CompiledSceneStorage storage{};
    compile(storage, scene.storage.polytree, scene.descs, {}, camera_at(0.f));

    ASSERT_EQ(storage.scene.splats.size(), 1u);

    set_world(scene.storage.polytree, scene.obj, translation(3.f, 4.f, 5.f));

    update_compiled_transforms(storage, scene.storage.polytree, scene.descs, camera_at(0.f));

    EXPECT_FLOAT_EQ(storage.scene.splats[0].position.x, 3.f);
    EXPECT_FLOAT_EQ(storage.scene.splats[0].position.y, 4.f);
    EXPECT_FLOAT_EQ(storage.scene.splats[0].position.z, 5.f);
}


// ─── update_compiled_transforms: view depths refreshed ───────────────────────

TEST(UpdateCompiledTransforms, ViewDepthsRefreshedAfterTransformUpdate)
{
    auto scene = make_scene(classify_legacy_renderable(RenderPipeline::TransparentGeometry));
    CompiledSceneStorage storage{};

    // Object at z=2, camera at z=0 → depth ≈ 2.
    compile(storage, scene.storage.polytree, scene.descs, {}, camera_at(0.f));
    ASSERT_EQ(storage.scene.transparent.size(), 1u);
    const float depth_before = storage.scene.transparent[0].depth;

    // Move object to z=-8 (in front of camera, positive depth in this convention).
    set_world(scene.storage.polytree, scene.obj, translation(0.f, 0.f, -8.f));
    update_compiled_transforms(storage, scene.storage.polytree, scene.descs, camera_at(0.f));

    const float depth_after = storage.scene.transparent[0].depth;
    EXPECT_GT(depth_after, depth_before)
        << "depth did not increase after moving transparent object further away";
}


// ─── update_compiled_transforms: stable buffer not reallocated ───────────────

TEST(UpdateCompiledTransforms, StableBufferNotReallocated)
{
    auto scene = make_scene(classify_legacy_renderable(RenderPipeline::OpaqueGeometry));
    CompiledSceneStorage storage{};
    compile(storage, scene.storage.polytree, scene.descs, {}, camera_at(0.f));

    const std::byte* stable_ptr = storage.stable_buffer.get();
    const std::byte* meta_ptr   = storage.metadata_buffer.get();

    set_world(scene.storage.polytree, scene.obj, translation(0.f, 0.f, 3.f));
    update_compiled_transforms(storage, scene.storage.polytree, scene.descs, camera_at(0.f));

    EXPECT_EQ(storage.stable_buffer.get(),   stable_ptr) << "stable_buffer reallocated";
    EXPECT_EQ(storage.metadata_buffer.get(), meta_ptr)   << "metadata_buffer reallocated";
}


// ─── refresh routing: Transform routes to update_compiled_transforms ──────────
//
// Proves routing by mutating a descriptor (mesh handle) after initial compile.
// A full compile would pick up the mutation; transform-update must not.

TEST(SceneRefresh, TransformDirtyRoutesToTransformUpdate)
{
    auto scene = make_scene(classify_legacy_renderable(RenderPipeline::OpaqueGeometry), /*mesh=*/5u);
    CompiledSceneStorage storage{};
    compile(storage, scene.storage.polytree, scene.descs, {}, camera_at(0.f));

    ASSERT_EQ(storage.scene.opaque[0].mesh, 5u);

    // Update world matrix.
    set_world(scene.storage.polytree, scene.obj, translation(0.f, 0.f, 9.f));

    // Mutate descriptor — compile() would pick this up, transform-update must not.
    scene.descs[scene.obj].mesh = 99u;

    refresh_compiled_scene(
        storage, scene.storage.polytree, scene.descs, {}, camera_at(0.f),
        SceneDirtyBits::Transform);

    EXPECT_FLOAT_EQ(storage.scene.opaque[0].world.m[14], 9.f)
        << "world matrix not updated — transform path not taken";
    EXPECT_EQ(storage.scene.opaque[0].mesh, 5u)
        << "mesh handle changed — compile() was called instead of transform update";
}


// ─── refresh routing: structural bits trigger full compile ────────────────────

TEST(SceneRefresh, VisibilityDirtyTriggersFullCompile)
{
    auto scene = make_scene(classify_legacy_renderable(RenderPipeline::OpaqueGeometry), /*mesh=*/5u);
    CompiledSceneStorage storage{};
    compile(storage, scene.storage.polytree, scene.descs, {}, camera_at(0.f));

    ASSERT_EQ(storage.scene.opaque[0].mesh, 5u);

    scene.descs[scene.obj].mesh = 77u;

    refresh_compiled_scene(
        storage, scene.storage.polytree, scene.descs, {}, camera_at(0.f),
        SceneDirtyBits::Visibility);

    EXPECT_EQ(storage.scene.opaque[0].mesh, 77u)
        << "mesh handle not updated — compile() was not called as expected";
}
