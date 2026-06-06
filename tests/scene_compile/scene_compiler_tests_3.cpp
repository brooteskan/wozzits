// file: tests/session7_object_rendering_test.cpp

#include <gtest/gtest.h>

#include <math/mat4.h>
#include <render/backend/stub_backend.h>
#include <render/frame/render_frame.h>
#include <render/ir/render_ir.h>
#include <scene/compile/scene_compiler.h>
#include <scene/compile/legacy_classification.h>
#include <scene/scene_graph.h>

using namespace wz::scene;
using namespace wz::render;
using namespace wz::render::backend;
using namespace wz::core::graph;
using namespace wz::math;

namespace {

    Mat4 translation(float x, float y, float z)
    {
        Mat4 m = Mat4::identity();
        m.m[12] = x;
        m.m[13] = y;
        m.m[14] = z;
        return m;
    }

    AABB unit_box()
    {
        return {
            Vec3{ -0.5f, -0.5f, -0.5f },
            Vec3{  0.5f,  0.5f,  0.5f }
        };
    }

    ViewData make_view()
    {
        ViewData v{};
        v.view = Mat4::identity();
        v.projection = Mat4::identity();
        // Wide orthographic VP: frustum ≈ ±10000 so test objects at any reasonable position are visible.
        v.view_projection.m[0]  = 1e-4f;
        v.view_projection.m[5]  = 1e-4f;
        v.view_projection.m[10] = 1e-4f;
        v.view_projection.m[15] = 1.0f;
        v.camera_position = Vec3{ 0.f, 0.f, -5.f };
        return v;
    }

} // namespace


TEST(Session7Spec, OneOpaqueSceneObjectProducesOneDrawCommand)
{
    constexpr MeshHandle     DebugTriangleMesh = 1u;
    constexpr MaterialHandle DebugOpaqueMaterial = 1u;

    SceneBuilder b;

    TransformNode object{};
    object.local = translation(2.f, 0.f, 3.f);
    object.motion_type = TransformNode::MotionType::Static;

    NodeHandle object_node = add_node(b, object);

    auto scene_storage = build(std::move(b));
    ASSERT_TRUE(scene_storage.has_value());

    propagate_all(scene_storage->polytree);

    std::vector<RenderableDescriptor> descs(node_count(scene_storage->polytree));
    descs[object_node] = RenderableDescriptor{
        .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
        .mesh = DebugTriangleMesh,
        .material = DebugOpaqueMaterial,
        .local_bounds = unit_box(),
        .splat_data = {},
        .visible = true,
    };

    CompiledSceneStorage compiled_storage;
    RenderIRStorage      ir_storage;
    RenderFrameStorage   frame_storage;

    auto cs    = compile(compiled_storage, scene_storage->polytree, descs, {}, make_view());
    auto ir    = build_render_ir(ir_storage, cs);
    auto frame = build_frame(frame_storage, ir, cs);

    ASSERT_EQ(cs.opaque.size(), 1u);
    ASSERT_EQ(ir.opaque.size(), 1u);
    ASSERT_EQ(frame.opaque.size(), 1u);

    const DrawCommand& cmd = frame.opaque[0];

    EXPECT_EQ(cmd.stage, PipelineStage::OpaqueGeometry);
    EXPECT_EQ(cmd.mesh, DebugTriangleMesh);
    EXPECT_EQ(cmd.material, DebugOpaqueMaterial);

    EXPECT_FLOAT_EQ(cmd.world.m[12], 2.f);
    EXPECT_FLOAT_EQ(cmd.world.m[13], 0.f);
    EXPECT_FLOAT_EQ(cmd.world.m[14], 3.f);

    EXPECT_FLOAT_EQ(frame.view.camera_position.z, -5.f);

    SubmitResult result = submit(frame);

    EXPECT_EQ(result.total(), 1u);
    EXPECT_EQ(result.opaque_count(), 1u);
    EXPECT_EQ(result.splat_count(), 0u);
    EXPECT_EQ(result.transparent_count(), 0u);
    EXPECT_EQ(result.particle_count(), 0u);
}


TEST(Session7Spec, MovingOpaqueSceneObjectUpdatesDrawCommandWorldTransform)
{
    constexpr MeshHandle     DebugTriangleMesh = 1u;
    constexpr MaterialHandle DebugOpaqueMaterial = 1u;

    SceneBuilder b;

    TransformNode object{};
    object.local = translation(0.f, 0.f, 0.f);
    object.motion_type = TransformNode::MotionType::Static;

    NodeHandle object_node = add_node(b, object);

    auto scene_storage = build(std::move(b));
    ASSERT_TRUE(scene_storage.has_value());

    auto& g = scene_storage->polytree;

    std::vector<RenderableDescriptor> descs(node_count(g));
    descs[object_node] = RenderableDescriptor{
        .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
        .mesh = DebugTriangleMesh,
        .material = DebugOpaqueMaterial,
        .local_bounds = unit_box(),
        .splat_data = {},
        .visible = true,
    };

    constexpr uint32_t Frame = 1u;

    set_local(g, object_node, translation(4.f, 0.f, 0.f));

    NodeHandle scratch[8]{};
    auto dirty_roots = collect_dirty_roots(g, Frame, scratch);
    update_static(g, dirty_roots, Frame);

    CompiledSceneStorage compiled_storage;
    RenderIRStorage      ir_storage;
    RenderFrameStorage   frame_storage;

    auto cs    = compile(compiled_storage, g, descs, {}, make_view());
    auto ir    = build_render_ir(ir_storage, cs);
    auto frame = build_frame(frame_storage, ir, cs);

    ASSERT_EQ(frame.opaque.size(), 1u);

    const DrawCommand& cmd = frame.opaque[0];

    EXPECT_EQ(cmd.stage, PipelineStage::OpaqueGeometry);
    EXPECT_EQ(cmd.mesh, DebugTriangleMesh);
    EXPECT_EQ(cmd.material, DebugOpaqueMaterial);

    EXPECT_FLOAT_EQ(cmd.world.m[12], 4.f);
    EXPECT_FLOAT_EQ(cmd.world.m[13], 0.f);
    EXPECT_FLOAT_EQ(cmd.world.m[14], 0.f);
}
