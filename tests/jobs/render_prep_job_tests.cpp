// tests/jobs/render_prep_job_tests.cpp

#include <gtest/gtest.h>

#include <jobs/job_graph_template.h>
#include <jobs/frame_execution.h>
#include <jobs/dag_scheduler.h>

#include <scene/compile/scene_compiler.h>
#include <render/ir/render_ir.h>
#include <render/frame/render_frame.h>

#include <math/mat4.h>
#include <math/projection.h>

namespace
{
    struct TestFrameStorage
    {
        wz::scene::ViewData             view{};
        wz::scene::CompiledSceneStorage compiled_scene{};
        wz::render::RenderIRStorage     render_ir{};
        wz::render::RenderFrameStorage  render_frame{};
    };

    struct RenderPrepJobData
    {
        TestFrameStorage* frame = nullptr;
        wz::scene::SceneStorage* scene = nullptr;
        std::vector<wz::scene::RenderableDescriptor>* descriptors = nullptr;
    };

    wz::scene::ViewData make_test_view()
    {
        using namespace wz::math;

        wz::scene::ViewData view{};

        view.camera_position = Vec3{ 0.0f, 0.0f, 0.0f };

        view.view = mat4_identity();

        constexpr float Pi = 3.14159265358979323846f;
        const float fov = 70.0f * Pi / 180.0f;
        const float aspect = 1280.0f / 720.0f;

        view.projection =
            wz::math::projection_perspective_dx(
                fov,
                aspect,
                0.1f,
                100.0f
            );

        view.view_projection = mul(view.projection, view.view);

        return view;
    }

    void job_build_view(wz::jobs::JobContext& ctx)
    {
        auto* data = static_cast<RenderPrepJobData*>(ctx.frame_user);
        ASSERT_NE(data, nullptr);
        ASSERT_NE(data->frame, nullptr);

        data->frame->view = make_test_view();
    }

    void job_compile_scene(wz::jobs::JobContext& ctx)
    {
        auto* data = static_cast<RenderPrepJobData*>(ctx.frame_user);
        ASSERT_NE(data, nullptr);
        ASSERT_NE(data->frame, nullptr);
        ASSERT_NE(data->scene, nullptr);
        ASSERT_NE(data->descriptors, nullptr);

        wz::scene::compile(
            data->frame->compiled_scene,
            data->scene->polytree,
            *data->descriptors,
            {},
            data->frame->view
        );
    }

    void job_build_render_ir(wz::jobs::JobContext& ctx)
    {
        auto* data = static_cast<RenderPrepJobData*>(ctx.frame_user);
        ASSERT_NE(data, nullptr);
        ASSERT_NE(data->frame, nullptr);

        wz::render::build_render_ir(
            data->frame->render_ir,
            data->frame->compiled_scene.scene
        );
    }

    void job_build_render_frame(wz::jobs::JobContext& ctx)
    {
        auto* data = static_cast<RenderPrepJobData*>(ctx.frame_user);
        ASSERT_NE(data, nullptr);
        ASSERT_NE(data->frame, nullptr);

        wz::render::build_frame(
            data->frame->render_frame,
            data->frame->render_ir.ir,
            data->frame->compiled_scene.scene
        );
    }

    struct TinyScene
    {
        wz::scene::SceneStorage scene{};
        std::vector<wz::scene::RenderableDescriptor> descriptors{};
        wz::core::graph::NodeHandle object_h = wz::core::graph::INVALID_NODE;
    };

    TinyScene make_tiny_opaque_scene()
    {
        using namespace wz::scene;
        using namespace wz::core::graph;
        using namespace wz::math;

        SceneBuilder b;

        TransformNode root{};
        root.local = mat4_identity();

        NodeHandle root_h = add_node(b, root);

        TransformNode object{};
        object.local = mat4_identity();
        object.local.m[14] = 3.0f;
        object.flags = TransformNodeFlag::RenderDomain;
        object.motion_type = TransformNode::MotionType::Static;

        NodeHandle object_h = add_node(b, object);

        EXPECT_TRUE(add_edge(b, root_h, object_h));

        auto scene_result = build(std::move(b));
        EXPECT_TRUE(scene_result.has_value());

        TinyScene out{};
        out.scene = std::move(*scene_result);
        out.object_h = object_h;

        out.descriptors.resize(node_count(out.scene.polytree));

        out.descriptors[root_h] = RenderableDescriptor{
            .pipeline = RenderPipeline::None,
        };

        out.descriptors[object_h] = RenderableDescriptor{
            .pipeline = RenderPipeline::OpaqueGeometry,
            .mesh = 0,
            .material = 0,
            .local_bounds = {},
            .splat_data = {},
            .visible = true,
        };

        propagate_all(out.scene.polytree);

        return out;
    }
}

TEST(RenderPrepJobs, BuildsNonEmptyRenderFrameForTinyOpaqueScene)
{
    TinyScene tiny = make_tiny_opaque_scene();

    TestFrameStorage frame{};

    RenderPrepJobData data{
        .frame = &frame,
        .scene = &tiny.scene,
        .descriptors = &tiny.descriptors,
    };

    wz::jobs::JobGraphTemplate graph;

    auto build_view = graph.add_job({
        .name = "build_view",
        .lane = wz::jobs::ExecutionLane::MainThread,
        .run = job_build_view,
        });

    auto compile_scene = graph.add_job({
        .name = "compile_scene",
        .lane = wz::jobs::ExecutionLane::MainThread,
        .run = job_compile_scene,
        });

    auto build_render_ir = graph.add_job({
        .name = "build_render_ir",
        .lane = wz::jobs::ExecutionLane::MainThread,
        .run = job_build_render_ir,
        });

    auto build_render_frame = graph.add_job({
        .name = "build_render_frame",
        .lane = wz::jobs::ExecutionLane::MainThread,
        .run = job_build_render_frame,
        });

    EXPECT_TRUE(graph.add_dependency(build_view, compile_scene));
    EXPECT_TRUE(graph.add_dependency(compile_scene, build_render_ir));
    EXPECT_TRUE(graph.add_dependency(build_render_ir, build_render_frame));

    ASSERT_TRUE(graph.commit());

    wz::jobs::FrameExecution exec;
    exec.reset(graph);

    exec.bind(build_view, &data);
    exec.bind(compile_scene, &data);
    exec.bind(build_render_ir, &data);
    exec.bind(build_render_frame, &data);

    wz::jobs::DagScheduler scheduler;
    scheduler.execute(graph, exec);

    ASSERT_EQ(exec.remaining_jobs, 0u);
    EXPECT_EQ(exec.status[build_view], wz::jobs::JobStatus::Done);
    EXPECT_EQ(exec.status[compile_scene], wz::jobs::JobStatus::Done);
    EXPECT_EQ(exec.status[build_render_ir], wz::jobs::JobStatus::Done);
    EXPECT_EQ(exec.status[build_render_frame], wz::jobs::JobStatus::Done);

    const auto& render_frame = frame.render_frame.frame;

    ASSERT_EQ(render_frame.commands.size(), 1u);

    const wz::render::DrawCommand& cmd = render_frame.commands[0];

    EXPECT_EQ(cmd.stage, wz::render::PipelineStage::OpaqueGeometry);
    EXPECT_EQ(cmd.mesh, 0u);
    EXPECT_EQ(cmd.material, 0u);

    // The object was placed at z = 3.0f in local/world space.
    EXPECT_FLOAT_EQ(cmd.world.m[14], 3.0f);
}