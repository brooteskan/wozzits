#include <gtest/gtest.h>
#include <scene/compile/scene_compiler.h>
#include <scene/compile/legacy_classification.h>

using namespace wz::scene;
using namespace wz::core::graph;
using namespace wz::math;

// ─── Helpers ──────────────────────────────────────────────────────────────────

namespace {

    ViewData camera_at(float z)
    {
        ViewData v{};
        v.view               = Mat4::identity();
        v.view.m[14]         = -z;
        v.projection         = Mat4::identity();
        v.view_projection    = Mat4::identity();
        v.camera_position    = Vec3{ 0.f, 0.f, z };
        return v;
    }

    struct SimpleScene
    {
        SceneStorage                      storage{};
        std::vector<RenderableDescriptor> descs{};
        NodeHandle                        root{};
        NodeHandle                        obj{};
    };

    SimpleScene make_one_opaque(MeshHandle mesh = 0u)
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

        SimpleScene out{};
        out.storage = std::move(*result);
        out.root    = root_h;
        out.obj     = obj_h;

        out.descs.resize(node_count(out.storage.polytree));
        out.descs[root_h] = RenderableDescriptor{ .node_class = classify_legacy_renderable(RenderPipeline::None) };
        out.descs[obj_h]  = RenderableDescriptor{
            .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
            .mesh     = mesh,
            .material = 0u,
            .visible  = true,
        };

        propagate_all(out.storage.polytree);
        return out;
    }

} // namespace


// ─── Test 6: View-only refresh does not rebuild stable data ───────────────────
//
// After an initial compile(), changing a descriptor and calling
// refresh_compiled_scene() with SceneDirtyBits::View must leave the compiled
// stable records unchanged — the descriptor mutation is only picked up by a
// full compile.
//
// This proves update_view() was called, not compile().

TEST(SceneRefresh, ViewOnlyDirtyDoesNotRebuildStableRecords)
{
    auto scene = make_one_opaque(/*mesh=*/0u);
    CompiledSceneStorage storage{};

    // Full initial compile with camera at z=1.
    compile(storage, scene.storage.polytree, scene.descs, {}, camera_at(1.f));

    ASSERT_EQ(storage.scene.opaque.size(), 1u);
    EXPECT_EQ(storage.scene.opaque[0].mesh, 0u);

    const std::byte* stable_ptr  = storage.stable_buffer.get();
    const std::byte* meta_ptr    = storage.metadata_buffer.get();

    // Mutate the descriptor — a full compile would pick this up.
    scene.descs[scene.obj].mesh = 99u;

    // Refresh with View-only dirty bits (camera moved to z=5).
    auto cs = refresh_compiled_scene(
        storage,
        scene.storage.polytree,
        scene.descs,
        {},
        camera_at(5.f),
        SceneDirtyBits::View);

    // Stable records must be unchanged — the descriptor mutation was not compiled.
    ASSERT_EQ(cs.opaque.size(), 1u);
    EXPECT_EQ(cs.opaque[0].mesh, 0u)
        << "stable opaque record was updated — compile() was called when it should not have been";

    // Buffers must not have been reallocated.
    EXPECT_EQ(storage.stable_buffer.get(),   stable_ptr) << "stable_buffer reallocated";
    EXPECT_EQ(storage.metadata_buffer.get(), meta_ptr)   << "metadata_buffer reallocated";

    // View data must reflect the new camera.
    EXPECT_FLOAT_EQ(cs.view.camera_position.z, 5.f)
        << "view was not updated by the view-only refresh";
}


// ─── None dirty is a no-op ────────────────────────────────────────────────────

TEST(SceneRefresh, NoDirtyBitsIsNoOp)
{
    auto scene = make_one_opaque(/*mesh=*/7u);
    CompiledSceneStorage storage{};

    compile(storage, scene.storage.polytree, scene.descs, {}, camera_at(1.f));

    const std::byte* stable_ptr = storage.stable_buffer.get();
    const size_t     stable_cap = storage.stable_bytes;
    const ViewData   old_view   = storage.scene.view;

    // Mutate descriptor and view — neither should be picked up.
    scene.descs[scene.obj].mesh = 42u;

    refresh_compiled_scene(
        storage,
        scene.storage.polytree,
        scene.descs,
        {},
        camera_at(99.f),
        SceneDirtyBits::None);

    EXPECT_EQ(storage.stable_buffer.get(), stable_ptr);
    EXPECT_EQ(storage.stable_bytes,        stable_cap);
    EXPECT_EQ(storage.scene.opaque[0].mesh, 7u)   << "stable records changed on no-op";
    EXPECT_FLOAT_EQ(storage.scene.view.camera_position.z, old_view.camera_position.z)
        << "view changed on no-op";
}


// ─── Structural dirty bit triggers full compile ───────────────────────────────
//
// Geometry is a compile-forcing bit: it signals that mesh/bounds may have
// changed in a way that affects packed-array composition. Note that Transform
// alone routes to update_compiled_transforms(), not compile() — that behaviour
// is covered by scene_transform_update_test_1.cpp.

TEST(SceneRefresh, GeometryDirtyTriggersFullCompile)
{
    auto scene = make_one_opaque(/*mesh=*/0u);
    CompiledSceneStorage storage{};

    compile(storage, scene.storage.polytree, scene.descs, {}, camera_at(1.f));

    // Mutate the descriptor — a full compile must pick this up.
    scene.descs[scene.obj].mesh = 55u;

    refresh_compiled_scene(
        storage,
        scene.storage.polytree,
        scene.descs,
        {},
        camera_at(1.f),
        SceneDirtyBits::Geometry);

    ASSERT_EQ(storage.scene.opaque.size(), 1u);
    EXPECT_EQ(storage.scene.opaque[0].mesh, 55u)
        << "stable records not updated — compile() was not called as expected";
}
