#include <gtest/gtest.h>

#include <scene/compile/scene_compiler.h>
#include <scene/compile/legacy_classification.h>
#include <render/ir/render_ir.h>
#include <render/frame/render_frame.h>

#include <math/mat4.h>
#include <math/projection.h>

using namespace wz::scene;
using namespace wz::render;
using namespace wz::math;
using namespace wz::core::graph;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{
    ViewData make_view()
    {
        ViewData v{};
        v.view            = Mat4::identity();
        v.projection      = projection_perspective_dx(1.22f, 1.777f, 0.1f, 100.f);
        v.view_projection = mul(v.projection, v.view);
        return v;
    }

    struct TinyScene
    {
        SceneStorage                      scene{};
        std::vector<RenderableDescriptor> descriptors{};
    };

    TinyScene make_tiny_opaque_scene()
    {
        SceneBuilder b;

        TransformNode root{};
        root.local = Mat4::identity();
        NodeHandle root_h = add_node(b, root);

        TransformNode obj{};
        obj.local       = Mat4::identity();
        obj.local.m[14] = 3.0f;
        obj.flags       = TransformNodeFlag::RenderDomain;
        obj.motion_type = TransformNode::MotionType::Static;
        NodeHandle obj_h = add_node(b, obj);

        EXPECT_TRUE(add_edge(b, root_h, obj_h));
        auto result = build(std::move(b));
        EXPECT_TRUE(result.has_value());

        TinyScene out{};
        out.scene = std::move(*result);
        out.descriptors.resize(node_count(out.scene.polytree));

        out.descriptors[root_h] = RenderableDescriptor{ .node_class = classify_legacy_renderable(RenderPipeline::None) };
        out.descriptors[obj_h]  = RenderableDescriptor{
            .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
            .mesh     = 0,
            .material = 0,
            .visible  = true,
        };

        propagate_all(out.scene.polytree);
        return out;
    }
}

// ---------------------------------------------------------------------------
// CompiledSceneStorage reuse
// ---------------------------------------------------------------------------

TEST(CompiledSceneStorage, ReusesBuffersWhenSizeDoesNotGrow)
{
    TinyScene tiny = make_tiny_opaque_scene();
    CompiledSceneStorage storage{};

    compile(storage, tiny.scene.polytree, tiny.descriptors, {}, make_view());

    const std::byte* stable_ptr_first = storage.stable_buffer.get();
    const std::byte* view_ptr_first   = storage.view_buffer.get();
    const size_t     stable_cap_first = storage.stable_bytes;
    const size_t     view_cap_first   = storage.view_bytes;

    ASSERT_NE(stable_ptr_first, nullptr);
    ASSERT_NE(view_ptr_first,   nullptr);
    ASSERT_GT(stable_cap_first, 0u);
    ASSERT_GT(view_cap_first,   0u);

    // Compile the same scene again — required sizes are identical.
    auto cs = compile(storage, tiny.scene.polytree, tiny.descriptors, {}, make_view());

    EXPECT_EQ(storage.stable_buffer.get(), stable_ptr_first) << "stable_buffer reallocated unexpectedly";
    EXPECT_EQ(storage.view_buffer.get(),   view_ptr_first)   << "view_buffer reallocated unexpectedly";
    EXPECT_EQ(storage.stable_bytes,        stable_cap_first);
    EXPECT_EQ(storage.view_bytes,          view_cap_first);

    // Output must still be valid.
    ASSERT_EQ(cs.opaque.size(), 1u);
}

// ---------------------------------------------------------------------------
// RenderIRStorage reuse
// ---------------------------------------------------------------------------

TEST(RenderIRStorage, ReusesBufferWhenSizeDoesNotGrow)
{
    TinyScene tiny = make_tiny_opaque_scene();
    CompiledSceneStorage compiled{};
    auto cs = compile(compiled, tiny.scene.polytree, tiny.descriptors, {}, make_view());

    RenderIRStorage storage{};
    build_render_ir(storage, cs);

    const std::byte* ptr_first = storage.buffer.get();
    const size_t     cap_first = storage.buffer_bytes;

    ASSERT_NE(ptr_first, nullptr);
    ASSERT_GT(cap_first, 0u);

    // Build again with the same scene — buffer must be reused.
    auto ir = build_render_ir(storage, cs);

    EXPECT_EQ(storage.buffer.get(),  ptr_first) << "RenderIRStorage buffer reallocated unexpectedly";
    EXPECT_EQ(storage.buffer_bytes,  cap_first);

    ASSERT_EQ(ir.opaque.size(), 1u);
}

// ---------------------------------------------------------------------------
// RenderFrameStorage reuse
// ---------------------------------------------------------------------------

TEST(RenderFrameStorage, ReusesStableBufferWhenSizeDoesNotGrow)
{
    TinyScene tiny = make_tiny_opaque_scene();
    CompiledSceneStorage compiled{};
    auto cs = compile(compiled, tiny.scene.polytree, tiny.descriptors, {}, make_view());

    RenderIRStorage ir_storage{};
    auto ir = build_render_ir(ir_storage, cs);

    RenderFrameStorage storage{};
    build_frame(storage, ir, cs);

    const std::byte* stable_ptr = storage.stable_buffer.get();
    const size_t     stable_cap = storage.stable_bytes;

    ASSERT_NE(stable_ptr, nullptr);
    ASSERT_GT(stable_cap, 0u);

    // Build again — same draw count, stable_buffer must be reused.
    auto frame = build_frame(storage, ir, cs);

    EXPECT_EQ(storage.stable_buffer.get(), stable_ptr) << "stable_buffer reallocated unexpectedly";
    EXPECT_EQ(storage.stable_bytes,        stable_cap);

    ASSERT_EQ(frame.opaque.size(), 1u);
}

TEST(RenderFrameStorage, UpdateFrameViewDoesNotReallocateStableBuffer)
{
    TinyScene tiny = make_tiny_opaque_scene();
    CompiledSceneStorage compiled{};
    auto cs = compile(compiled, tiny.scene.polytree, tiny.descriptors, {}, make_view());

    RenderIRStorage ir_storage{};
    auto ir = build_render_ir(ir_storage, cs);

    RenderFrameStorage storage{};
    build_frame(storage, ir, cs);

    const std::byte* stable_ptr = storage.stable_buffer.get();
    const size_t     stable_cap = storage.stable_bytes;

    // update_frame_view must not touch stable_buffer at all.
    update_frame_view(storage, ir, cs);

    EXPECT_EQ(storage.stable_buffer.get(), stable_ptr) << "stable_buffer reallocated by update_frame_view";
    EXPECT_EQ(storage.stable_bytes,        stable_cap);
    EXPECT_EQ(storage.frame.opaque.size(), 1u);
}

// ---------------------------------------------------------------------------
// Growth: verify that a larger scene forces reallocation
// ---------------------------------------------------------------------------

TEST(RenderIRStorage, ReallocatesWhenSizeGrows)
{
    // Build a tiny 1-object scene first.
    TinyScene tiny = make_tiny_opaque_scene();
    CompiledSceneStorage compiled{};
    auto cs_small = compile(compiled, tiny.scene.polytree, tiny.descriptors, {}, make_view());

    RenderIRStorage storage{};
    build_render_ir(storage, cs_small);

    const std::byte* ptr_small = storage.buffer.get();
    const size_t     cap_small = storage.buffer_bytes;

    // Now synthesise a view with more opaque draws by building a larger compiled
    // scene directly — easiest approach is to call build_render_ir with a view
    // whose span sizes are larger, which we can do by building a 2-object scene.
    SceneBuilder b;
    TransformNode root{}; root.local = Mat4::identity();
    NodeHandle root_h = add_node(b, root);
    for (int i = 0; i < 3; ++i)
    {
        TransformNode obj{};
        obj.local       = Mat4::identity();
        obj.local.m[14] = static_cast<float>(i + 1);
        obj.flags       = TransformNodeFlag::RenderDomain;
        obj.motion_type = TransformNode::MotionType::Static;
        NodeHandle obj_h = add_node(b, obj);
        EXPECT_TRUE(add_edge(b, root_h, obj_h));
    }
    auto big_result = build(std::move(b));
    ASSERT_TRUE(big_result.has_value());
    SceneStorage big_scene = std::move(*big_result);

    std::vector<RenderableDescriptor> big_descs(node_count(big_scene.polytree));
    for (NodeHandle n : topo_order(big_scene.polytree))
    {
        if (n == root_h)
            big_descs[n] = RenderableDescriptor{ .node_class = classify_legacy_renderable(RenderPipeline::None) };
        else
            big_descs[n] = RenderableDescriptor{
                .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
                .mesh     = static_cast<uint32_t>(n),
                .material = 0,
                .visible  = true,
            };
    }
    propagate_all(big_scene.polytree);

    CompiledSceneStorage compiled_big{};
    auto cs_big = compile(compiled_big, big_scene.polytree, big_descs, {}, make_view());

    build_render_ir(storage, cs_big);

    EXPECT_GT(storage.buffer_bytes, cap_small) << "expected reallocation for larger scene";
    EXPECT_NE(storage.buffer.get(), ptr_small) << "expected new pointer after reallocation";
}
