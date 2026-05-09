#pragma once
// engine/game_app.h

#include <gpu/gpu_types.h>

#include <engine/app_context.h>
#include <engine/engine.h>
#include <engine/runtime_camera.h>

#include <scene/compile/scene_compiler.h>
#include <render/frame/render_frame.h>

#include <jobs/job_graph_template.h>
#include <jobs/frame_execution.h>
#include <jobs/dag_scheduler.h>


namespace wz::app
{

    struct DebugObjectRuntime
    {
        wz::scene::SceneStorage scene{};
        std::vector<wz::scene::RenderableDescriptor> descriptors{};

        bool ready = false;
        bool transforms_dirty = false;
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

        bool ready = false;
    };

    struct GameApp
    {
        wz::engine::AppContext ctx{};

        RuntimeCamera camera{};

        ScalarFieldDebugRuntime scalar_debug{};
        DebugObjectRuntime      debug_object{};
        AppJobRuntime           jobs{};
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