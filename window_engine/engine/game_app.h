#pragma once
// engine/game_app.h

#include <gpu/gpu_types.h>

#include <engine/app_context.h>
#include <engine/engine.h>
#include <engine/runtime_camera.h>

#include <scene/compile/scene_compiler.h>
#include <render/frame/render_frame.h>
#include <render/ir/render_ir.h>

#include <jobs/job_graph_template.h>
#include <jobs/frame_execution.h>
#include <jobs/dag_scheduler.h>
#include <jobs/job_profiler.h>


namespace wz::app
{
    // Chooses between a full scene rebuild and a camera-only view refresh.
    // Set each frame by job_compile_scene; read by job_build_render_ir and
    // job_build_render_frame to select the matching incremental call.
    // new: transform can change on objects
    enum class RenderPrepPath
    {
        FullCompile,
        ViewOnly,
        TransformOnly,
        TransformAndView,
    };

    struct DebugObjectRuntime
    {
        wz::scene::SceneStorage scene{};
        std::vector<wz::scene::RenderableDescriptor> descriptors{};

        std::vector<wz::core::graph::NodeHandle> animated_nodes{};
        std::vector<wz::math::Vec3> animated_base_positions{};
        std::vector<wz::core::graph::NodeHandle> transform_affected_nodes{};

        bool ready = false;
        bool transforms_dirty = false;
        bool compiled_scene_valid = false;
    };

    struct ScalarFieldDebugRuntime
    {
        wz::gpu::GPUHandle texture{};
        bool ready = false;
    };

    struct AppJobRuntime
    {
        wz::jobs::JobGraphTemplate graph{};
        wz::jobs::FrameExecution   exec{};
        wz::jobs::DagScheduler     scheduler{};

        wz::jobs::NodeHandle platform_events = wz::jobs::INVALID_JOB;
        wz::jobs::NodeHandle shutdown_input = wz::jobs::INVALID_JOB;
        wz::jobs::NodeHandle camera_update = wz::jobs::INVALID_JOB;

        wz::jobs::NodeHandle build_view = wz::jobs::INVALID_JOB;
        wz::jobs::NodeHandle compile_scene = wz::jobs::INVALID_JOB;
        wz::jobs::NodeHandle build_render_ir = wz::jobs::INVALID_JOB;
        wz::jobs::NodeHandle build_render_frame = wz::jobs::INVALID_JOB;

        wz::jobs::FrameJobProfile profile{};

        bool ready = false;
    };

    // FrameStorage — owns all CPU-side per-frame products.
    // Allocated once; overwritten each frame by compile/build_render_ir/build_frame.
    // Must outlive all jobs that read from it in a given frame.
    struct FrameStorage
    {
        wz::scene::ViewData             view{};

        wz::scene::CompiledSceneStorage compiled_scene;
        wz::render::RenderIRStorage     render_ir;
        wz::render::RenderFrameStorage  render_frame;

        RenderPrepPath render_prep_path = RenderPrepPath::FullCompile;
    };

    struct GameApp
    {
        wz::engine::AppContext ctx{};

        RuntimeCamera camera{};

        ScalarFieldDebugRuntime scalar_debug{};
        DebugObjectRuntime      debug_object{};
        AppJobRuntime           jobs{};
        FrameStorage            frame{};
    };

    bool init(GameApp& app);

    void update(
        wz::engine::Context& ctx,
        wz::engine::FrameContext& fctx,
        GameApp& app);

    void render(
        GameApp& app,
        const wz::engine::FrameContext& fctx);

    void shutdown(GameApp& app);
}