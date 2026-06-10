#pragma once
// bench/benchmark_app.h

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <engine/app_context.h>
#include <engine/engine.h>
#include <engine/frame_storage.h>
#include <engine/frame_types.h>
#include <bench/flying_camera.h>

#include <scene/scene_graph.h>
#include <scene/compile/scene_compiler.h>
#include <render/frame/render_frame.h>
#include <render/ir/render_ir.h>

#include <jobs/job_graph_template.h>
#include <jobs/frame_execution.h>
#include <jobs/dag_scheduler.h>
#include <jobs/job_profiler.h>

namespace wz::bench
{
    using RenderPrepPath = wz::engine::RenderPrepPath;
    using FrameDirtyState = wz::engine::FrameDirtyState;
    using BenchFrameStorage = wz::engine::FrameStorage;

    // Owns the CPU-side scene data for one benchmark scene.
    // Populated by SceneDefinition::build; updated by SceneDefinition::update.
    struct SceneRuntime
    {
        wz::scene::SceneStorage                      scene{};
        std::vector<wz::scene::RenderableDescriptor> descriptors{};
        std::vector<wz::core::graph::NodeHandle>     dirty_nodes{};

        bool ready               = false;
        bool compiled_scene_valid = false;
        bool transforms_dirty    = false;
        bool first_compile_logged = false;
    };

    // Supplied by the caller to define what scene the app renders.
    struct SceneDefinition
    {
        std::string name;

        // Optional. Called during init(), before commit(), while the asset
        // registration phase is still open. Use this to register any extra
        // assets (e.g. gaussian splat shaders) that must share the same commit
        // as the built-in debug shaders. Return false to abort init.
        std::function<bool(wz::engine::assets::EngineAssetLibrary&)> pre_commit;

        // Required. Populate runtime.scene and runtime.descriptors, then call
        // propagate_all(). Return false to abort init.
        std::function<bool(SceneRuntime&, wz::engine::AppContext&)> build;

        // Optional. Called every frame before compile_scene.
        // Push changed NodeHandles into runtime.dirty_nodes; leave it empty for a
        // static scene.
        std::function<void(SceneRuntime&, float dt)> update;
    };

    struct BenchJobRuntime
    {
        wz::jobs::JobGraphTemplate graph{};
        wz::jobs::FrameExecution   exec{};
        wz::jobs::DagScheduler     scheduler{};

        wz::jobs::NodeHandle platform_events  = wz::jobs::INVALID_JOB;
        wz::jobs::NodeHandle shutdown_input   = wz::jobs::INVALID_JOB;
        wz::jobs::NodeHandle camera_update    = wz::jobs::INVALID_JOB;
        wz::jobs::NodeHandle build_view       = wz::jobs::INVALID_JOB;
        wz::jobs::NodeHandle compile_scene    = wz::jobs::INVALID_JOB;
        wz::jobs::NodeHandle build_render_ir  = wz::jobs::INVALID_JOB;
        wz::jobs::NodeHandle build_render_frame = wz::jobs::INVALID_JOB;

        wz::jobs::FrameJobProfile profile{};

        bool ready = false;
    };

    struct BenchmarkApp
    {
        wz::engine::AppContext ctx{};

        FlyingCamera camera{};
        bool         navigation_active = false;

        SceneDefinition scene_def{};
        SceneRuntime    scene_runtime{};
        BenchJobRuntime jobs{};
        FrameDirtyState frame_dirty{};
        BenchFrameStorage frame{};
    };

    // Pass ownership of the SceneDefinition at init time.
    bool init(BenchmarkApp& app, SceneDefinition scene_def);

    void update(
        wz::engine::Context&      ctx,
        wz::engine::FrameContext& fctx,
        BenchmarkApp&             app);

    // Submit the scene's render frame without wrapping begin/end_frame.
    // Call this between gpu::begin_frame and gpu::end_frame when hosting
    // inside a toolhost that owns the frame boundary.
    void render_contents(
        BenchmarkApp&                   app,
        const wz::engine::FrameContext& fctx);

    // Standalone render: wraps begin_frame / clear / render_contents / end_frame / present.
    void render(
        BenchmarkApp&                   app,
        const wz::engine::FrameContext& fctx);

    void shutdown(BenchmarkApp& app);
}
