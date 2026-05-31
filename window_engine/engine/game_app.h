#pragma once
// engine/game_app.h

#include <cstdint>

#include <gpu/gpu_types.h>

#include <engine/app_context.h>
#include <engine/engine.h>
#include <engine/frame_storage.h>
#include <engine/frame_types.h>
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
    using RenderPrepPath = wz::engine::RenderPrepPath;
    using FrameStorage = wz::engine::FrameStorage;

    enum class DebugSceneAnimation
    {
        Static,
        Leaves,
        ParentSubtree,
    };

    struct DebugSceneConfig
    {
        const char* name = "";
        uint32_t object_count = 1000;
        DebugSceneAnimation animation = DebugSceneAnimation::Leaves;
        bool culling_disabled = false;
        bool wide_layout = false;
    };

    struct DebugObjectRuntime
    {
        wz::scene::SceneStorage scene{};
        std::vector<wz::scene::RenderableDescriptor> descriptors{};

        const DebugSceneConfig* config = nullptr;
        uint32_t mode_index = 0;
        uint32_t renderable_count = 0;

        std::vector<wz::core::graph::NodeHandle> animated_parent_nodes{};
        std::vector<wz::math::Vec3> animated_parent_base_positions{};
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

    void render_contents(
        GameApp& app,
        const wz::engine::FrameContext& fctx);

    void shutdown(GameApp& app);
}
