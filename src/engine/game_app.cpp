// src/engine/game_app.cpp
#include <event/platform_event.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <iomanip>
#include <sstream>
#include <string>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <engine/game_app.h>
#include <engine/runtime_camera.h>
#include <math/projection.h>

#include <gpu/scalar_field_texture.h>
#include <gpu/dx12/dx12.h>
#include <engine/assets/scalar_field/scalar_field.h>

#include <stats/scene_render_storage.h>
#include <scene/compile/legacy_classification.h>

#define INIT_FAIL(msg) \
    do { OutputDebugStringA("GameApp init failed: " msg "\n"); return false; } while (0)

namespace wz::app
{
    namespace
    {
        static bool g_logged_job_update_once = false;
        constexpr uint32_t kDebugObjectCount = 1000;
        constexpr uint32_t kAnimatedDebugObjectCount = 10;

        struct AppUpdateFrameData
        {
            wz::engine::Context* ctx = nullptr;
            wz::engine::FrameContext* fctx = nullptr;
            wz::app::GameApp* app = nullptr;
            float                     dt = 0.0f;
        };

        void update_world_for_nodes(
            wz::scene::SceneGraph& g,
            std::span<const wz::core::graph::NodeHandle> nodes)
        {
            for (wz::core::graph::NodeHandle n : nodes)
            {
                auto& node = const_cast<wz::scene::TransformNode&>(
                    wz::core::graph::node_data(g, n));

                const wz::core::graph::NodeHandle p =
                    wz::core::graph::parent(g, n);

                node.world =
                    (p == wz::core::graph::INVALID_NODE)
                    ? node.local
                    : wz::scene::compute_world(
                        wz::core::graph::node_data(g, p).world,
                        node.local);
            }
        }

        void update_debug_object_animation(
            wz::app::GameApp& app,
            float t)
        {
            auto& dbg = app.debug_object;

            if (!dbg.ready)
                return;

            dbg.transform_affected_nodes.clear();

            const uint32_t count =
                static_cast<uint32_t>(dbg.animated_nodes.size());

            for (uint32_t i = 0; i < count; ++i)
            {
                const auto n = dbg.animated_nodes[i];
                const auto base = dbg.animated_base_positions[i];

                wz::math::Mat4 local = wz::math::mat4_identity();

                local.m[12] = base.x;
                local.m[13] = base.y + 0.5f * std::sin(t * 2.0f + static_cast<float>(i));
                local.m[14] = base.z;

                wz::scene::set_local(dbg.scene.polytree, n, local);

                dbg.transform_affected_nodes.push_back(n);
            }

            dbg.transforms_dirty = !dbg.transform_affected_nodes.empty();
        }

        bool build_debug_object_scene(
            wz::app::GameApp& app,
            uint32_t object_count)
        {
            using namespace wz::scene;
            using namespace wz::core::graph;
            using namespace wz::math;

            app.debug_object.animated_nodes.clear();
            app.debug_object.animated_base_positions.clear();
            app.debug_object.transform_affected_nodes.clear();

            SceneBuilder b;

            TransformNode root{};
            root.local = mat4_identity();

            NodeHandle root_h = add_node(b, root);

            std::vector<NodeHandle> object_nodes;
            object_nodes.reserve(object_count);

            const uint32_t columns = 32;
            const float spacing = 1.25f;

            for (uint32_t i = 0; i < object_count; ++i)
            {
                const uint32_t x = i % columns;
                const uint32_t y = i / columns;

                TransformNode object{};
                object.local = mat4_identity();

                object.local.m[12] =
                    (static_cast<float>(x) - static_cast<float>(columns) * 0.5f) * spacing;

                object.local.m[13] =
                    (static_cast<float>(y) - static_cast<float>(object_count / columns) * 0.5f) * spacing;

                object.local.m[14] = 8.0f;

                object.flags = TransformNodeFlag::RenderDomain;
                object.motion_type = TransformNode::MotionType::Static;

                NodeHandle object_h = add_node(b, object);
                add_edge(b, root_h, object_h);

                if (i < kAnimatedDebugObjectCount)
                {
                    app.debug_object.animated_nodes.push_back(object_h);

                    app.debug_object.animated_base_positions.push_back(
                        wz::math::Vec3{
                            object.local.m[12],
                            object.local.m[13],
                            object.local.m[14],
                        }
                        );
                }

                object_nodes.push_back(object_h);
            }

            auto scene_result = build(std::move(b));
            if (!scene_result.has_value())
                return false;

            app.debug_object.scene = std::move(*scene_result);

            app.debug_object.descriptors.clear();
            app.debug_object.descriptors.resize(
                node_count(app.debug_object.scene.polytree)
            );

            app.debug_object.descriptors[root_h] = RenderableDescriptor{
                .node_class = classify_legacy_renderable(RenderPipeline::None),
            };

            for (NodeHandle object_h : object_nodes)
            {
                app.debug_object.descriptors[object_h] = RenderableDescriptor{
                    .node_class = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
                    .mesh = 0,
                    .material = 0,
                    .local_bounds = {
                        .min = Vec3{ -0.5f, -0.5f, -0.5f },
                        .max = Vec3{  0.5f,  0.5f,  0.5f },
                    },
                    .splat_data = {},
                    .visible = true,
                };
            }

            propagate_all(app.debug_object.scene.polytree);

            app.debug_object.ready = true;
            app.debug_object.transforms_dirty = false;

            return true;
        }

        wz::scene::ViewData build_view_data(
            const wz::app::RuntimeCamera& camera,
            const wz::engine::FrameContext& fctx)
        {
            using namespace wz::scene;
            using namespace wz::math;

            ViewData view{};
            view.camera_position = Vec3{ camera.x, camera.y, 0.0f };

            view.view = mat4_identity();
            view.view.m[12] = -camera.x;
            view.view.m[13] = -camera.y;
            view.view.m[14] = 0.0f;

            constexpr float Pi = 3.14159265358979323846f;
            const float fov = 70.0f * Pi / 180.0f;

            const int win_w = fctx.input.window.width;
            const int win_h = fctx.input.window.height;

            const float aspect =
                (win_w > 0 && win_h > 0)
                ? static_cast<float>(win_w) / static_cast<float>(win_h)
                : 1280.0f / 720.0f;

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

        constexpr uint64_t kJobProfileLogEveryNFrames = 120;

        double ticks_to_ms(uint64_t ticks)
        {
            const double ticks_per_second =
                static_cast<double>(wz::time::TimeSource::ticks_per_second());

            return (static_cast<double>(ticks) * 1000.0) / ticks_per_second;
        }

        struct FrameAllocationSummary
        {
            uint64_t bytes_owned = 0;
            uint64_t bytes_allocated_this_frame = 0;
            uint32_t reallocations_this_frame = 0;
        };

        void reset_frame_allocation_counters(wz::app::GameApp& app)
        {
            app.frame.compiled_scene.stable_stats.reset_build_counters();
            app.frame.compiled_scene.view_stats.reset_build_counters();
            app.frame.compiled_scene.metadata_stats.reset_build_counters();
            app.frame.render_ir.stats.reset_build_counters();
            app.frame.render_frame.stable_stats.reset_build_counters();
            app.frame.render_frame.view_stats.reset_build_counters();
        }

        FrameAllocationSummary summarize_frame_allocations(
            const wz::app::GameApp& app)
        {
            FrameAllocationSummary out{};

            auto add = [&](const wz::scene_render::AllocationStats& s)
                {
                    out.bytes_owned += s.bytes_owned;
                    out.bytes_allocated_this_frame += s.bytes_allocated_this_build;
                    out.reallocations_this_frame += s.reallocations_this_build;
                };

            add(app.frame.compiled_scene.stable_stats);
            add(app.frame.compiled_scene.view_stats);
            add(app.frame.render_ir.stats);
            add(app.frame.render_frame.stable_stats);
            add(app.frame.render_frame.view_stats);

            return out;
        }

        const char* render_prep_path_name(wz::app::RenderPrepPath path)
        {
            switch (path)
            {
            case wz::app::RenderPrepPath::FullCompile:
                return "FullCompile";

            case wz::app::RenderPrepPath::ViewOnly:
                return "ViewOnly";

            case wz::app::RenderPrepPath::TransformOnly:
                return "TransformOnly";

            case wz::app::RenderPrepPath::TransformAndView:
                return "TransformAndView";

            default:
                return "Unknown";
            }
        }

        void log_job_profile_if_due(
            wz::app::GameApp& app,
            const wz::jobs::FrameJobProfile& profile)
        {
            if (profile.timings.empty())
                return;

            if ((profile.frame_index % kJobProfileLogEveryNFrames) != 0)
                return;

            uint64_t total_ticks = 0;
            uint64_t render_prep_ticks = 0;
            const wz::jobs::JobTimingRecord* slowest = nullptr;

            for (const auto& rec : profile.timings)
            {
                const uint64_t dt = rec.duration_ticks();
                total_ticks += dt;

                const char* name = rec.name ? rec.name : "";

                if (
                    std::strcmp(name, "build_view") == 0 ||
                    std::strcmp(name, "compile_scene") == 0 ||
                    std::strcmp(name, "build_render_ir") == 0 ||
                    std::strcmp(name, "build_render_frame") == 0)
                {
                    render_prep_ticks += dt;
                }

                if (!slowest || dt > slowest->duration_ticks())
                    slowest = &rec;
            }

            const auto allocs = summarize_frame_allocations(app);

            const auto scene_nodes =
                app.debug_object.ready
                ? wz::core::graph::node_count(app.debug_object.scene.polytree)
                : 0;

            std::ostringstream summary;
            summary << std::fixed << std::setprecision(3);

            const auto cmd_count =
                app.frame.render_frame.frame.opaque.size()
                + app.frame.render_frame.frame.splats.size()
                + app.frame.render_frame.frame.transparent.size()
                + app.frame.render_frame.frame.particles.size();

            summary
                << "jobs frame=" << profile.frame_index
                << " path=" << render_prep_path_name(app.frame.render_prep_path)
                << " dirty_nodes=" << app.debug_object.transform_affected_nodes.size()
                << " total=" << ticks_to_ms(total_ticks) << " ms"
                << " prep=" << ticks_to_ms(render_prep_ticks) << " ms"
                << " jobs=" << profile.timings.size()
                << " nodes=" << scene_nodes
                << " cmds=" << cmd_count
                << " allocs=" << allocs.reallocations_this_frame
                << " alloc_bytes=" << allocs.bytes_allocated_this_frame
                << " owned=" << allocs.bytes_owned;

            if (slowest)
            {
                summary
                    << " slowest="
                    << (slowest->name ? slowest->name : "<unnamed>")
                    << " "
                    << ticks_to_ms(slowest->duration_ticks())
                    << " ms";
            }

            app.ctx.logger.info(summary.str());

            for (const auto& rec : profile.timings)
            {
                std::ostringstream line;
                line << std::fixed << std::setprecision(3);

                line
                    << "  job "
                    << (rec.name ? rec.name : "<unnamed>")
                    << " "
                    << ticks_to_ms(rec.duration_ticks())
                    << " ms";

                app.ctx.logger.info(line.str());
            }
        }

        void job_build_view(wz::jobs::JobContext& ctx)
        {
            auto* data = static_cast<AppUpdateFrameData*>(ctx.frame_user);
            assert(data);
            assert(data->fctx);
            assert(data->app);

            data->app->frame.view =
                build_view_data(data->app->camera, *data->fctx);
        }

        void job_compile_scene(wz::jobs::JobContext& ctx)
        {
            auto* data = static_cast<AppUpdateFrameData*>(ctx.frame_user);
            assert(data);
            assert(data->app);

            auto& app = *data->app;
            auto& dbg = app.debug_object;

            if (!dbg.ready)
                return;

            if (!dbg.compiled_scene_valid)
            {
                wz::scene::compile(
                    app.frame.compiled_scene,
                    dbg.scene.polytree,
                    dbg.descriptors,
                    {},
                    app.frame.view
                );

                dbg.compiled_scene_valid = true;
                dbg.transforms_dirty = false;

                app.frame.render_prep_path = RenderPrepPath::FullCompile;
                return;
            }

            // Debug animation currently moves only leaf renderable nodes.
            // If parent nodes become animated, replace this with dirty-root subtree
            // propagation and include all affected descendant renderable nodes.
            if (dbg.transforms_dirty)
            {
                update_world_for_nodes(
                    dbg.scene.polytree,
                    dbg.transform_affected_nodes
                );

                wz::scene::update_compiled_transforms(
                    app.frame.compiled_scene,
                    dbg.scene.polytree,
                    dbg.descriptors,
                    app.frame.view,
                    dbg.transform_affected_nodes,
                    true
                );

                dbg.transforms_dirty = false;

                app.frame.render_prep_path = RenderPrepPath::TransformAndView;
                return;
            }

            wz::scene::update_view(
                app.frame.compiled_scene,
                app.frame.view
            );

            app.frame.render_prep_path = RenderPrepPath::ViewOnly;
        }

        void job_build_render_ir(wz::jobs::JobContext& ctx)
        {
            auto* data = static_cast<AppUpdateFrameData*>(ctx.frame_user);
            assert(data);
            assert(data->app);

            if (!data->app->debug_object.ready)
                return;

            if (data->app->frame.render_prep_path == RenderPrepPath::FullCompile)
            {
                wz::render::build_render_ir(
                    data->app->frame.render_ir,
                    data->app->frame.compiled_scene.scene
                );
            }
            else
            {
                wz::render::update_render_ir(data->app->frame.render_ir, data->app->frame.compiled_scene.scene);
            }
        }

        void job_build_render_frame(wz::jobs::JobContext& ctx)
        {
            auto* data = static_cast<AppUpdateFrameData*>(ctx.frame_user);
            assert(data);
            assert(data->app);

            if (!data->app->debug_object.ready)
                return;

            if (data->app->frame.render_prep_path == RenderPrepPath::FullCompile)
            {
                wz::render::build_frame(
                    data->app->frame.render_frame,
                    data->app->frame.render_ir.ir,
                    data->app->frame.compiled_scene.scene
                );
            }
            else
            {
                wz::render::update_frame_view(
                    data->app->frame.render_frame,
                    data->app->frame.render_ir.ir,
                    data->app->frame.compiled_scene.scene
                );
            }
        }

        void job_platform_events(wz::jobs::JobContext& ctx)
        {
            auto* data = static_cast<AppUpdateFrameData*>(ctx.frame_user);
            assert(data);
            assert(data->ctx);
            assert(data->app);

            wz::window::pump_messages();

            PlatformEvent event{};
            while (wz::window::poll_event(data->app->ctx.window, event))
            {
                switch (event.type)
                {
                case PlatformEvent::Type::Close:
                    data->ctx->running = false;
                    break;

                case PlatformEvent::Type::Resize:
                    if (event.resize.width > 0 && event.resize.height > 0)
                    {
                        wz::gpu::resize(
                            data->app->ctx.device,
                            event.resize.width,
                            event.resize.height
                        );
                    }
                    break;

                default:
                    break;
                }
            }

            if (wz::window::window_should_close(data->app->ctx.window))
                data->ctx->running = false;
        }

        void job_shutdown_input(wz::jobs::JobContext& ctx)
        {
            auto* data = static_cast<AppUpdateFrameData*>(ctx.frame_user);
            assert(data);
            assert(data->ctx);
            assert(data->fctx);

            if (data->fctx->input.keyboard.pressed[VK_ESCAPE])
                data->ctx->running = false;
        }

        void job_update_camera(wz::jobs::JobContext& ctx)
        {
            auto* data = static_cast<AppUpdateFrameData*>(ctx.frame_user);
            assert(data);
            assert(data->fctx);
            assert(data->app);

            wz::app::update_camera(
                data->app->camera,
                data->fctx->input,
                data->dt
            );
        }

        bool build_app_jobs(wz::app::GameApp& app)
        {
            auto& jobs = app.jobs;

            jobs.platform_events = jobs.graph.add_job({
                .name = "platform_events",
                .lane = wz::jobs::ExecutionLane::MainThread,
                .run = job_platform_events,
                });

            jobs.shutdown_input = jobs.graph.add_job({
                .name = "shutdown_input",
                .lane = wz::jobs::ExecutionLane::MainThread,
                .run = job_shutdown_input,
                });

            jobs.camera_update = jobs.graph.add_job({
                .name = "camera_update",
                .lane = wz::jobs::ExecutionLane::MainThread,
                .run = job_update_camera,
                });

            jobs.build_view = jobs.graph.add_job({
                .name = "build_view",
                .lane = wz::jobs::ExecutionLane::MainThread,
                .run = job_build_view,
                });

            jobs.compile_scene = jobs.graph.add_job({
                .name = "compile_scene",
                .lane = wz::jobs::ExecutionLane::MainThread,
                .run = job_compile_scene,
                });

            jobs.build_render_ir = jobs.graph.add_job({
                .name = "build_render_ir",
                .lane = wz::jobs::ExecutionLane::MainThread,
                .run = job_build_render_ir,
                });

            jobs.build_render_frame = jobs.graph.add_job({
                .name = "build_render_frame",
                .lane = wz::jobs::ExecutionLane::MainThread,
                .run = job_build_render_frame,
                });

            // Platform/window messages should run before input-derived app behavior.
            jobs.graph.add_dependency(jobs.platform_events, jobs.shutdown_input);
            jobs.graph.add_dependency(jobs.shutdown_input, jobs.camera_update);
            jobs.graph.add_dependency(jobs.camera_update, jobs.build_view);
            jobs.graph.add_dependency(jobs.build_view, jobs.compile_scene);
            jobs.graph.add_dependency(jobs.compile_scene, jobs.build_render_ir);
            jobs.graph.add_dependency(jobs.build_render_ir, jobs.build_render_frame);
            jobs.ready = jobs.graph.commit();

            if (jobs.ready)
            {
                app.ctx.logger.info(
                    "app job graph committed: platform_events -> shutdown_input -> camera_update -> build_view -> compile_scene -> build_render_ir -> build_render_frame"
                );
            }
            else
            {
                app.ctx.logger.error("app update job graph commit failed");
            }

            return jobs.ready;
        }



        wz::render::RenderFrameView build_debug_object_frame(
            wz::app::GameApp& app,
            const wz::scene::ViewData& view)
        {
            using namespace wz::scene;
            using namespace wz::render;

            auto cs = compile(
                app.frame.compiled_scene,
                app.debug_object.scene.polytree,
                app.debug_object.descriptors,
                {},
                view
            );

            auto ir = build_render_ir(
                app.frame.render_ir,
                cs
            );

            return build_frame(
                app.frame.render_frame,
                ir,
                cs
            );
        }

    } // anonymous namespace

    bool init(GameApp& app)
    {
        if (!wz::engine::init(app.ctx, {
                .window = {
                    .title  = "Wozzits Runtime",
                    .width  = 1280,
                    .height = 720,
                },
                .resource_root = "resources",
            }))
            return false;

        using namespace wz::engine::assets;

        auto object_shaders = app.ctx.assets->shaders().create_shader_pair({
            .name         = "debug_opaque_object",
            .vertex_path  = "shaders/_debug/debug_object_vs.hlsl",
            .pixel_path   = "shaders/_debug/debug_object_ps.hlsl",
            });

        if (!object_shaders.valid())
            INIT_FAIL("create_shader_pair(debug_opaque_object)");

        if (!app.ctx.assets->commit())
            INIT_FAIL("assets commit");

        app.ctx.assets->resolve_all();

        auto object_shader_handles =
            app.ctx.assets->shaders().get_shader_pair(object_shaders);

        if (!object_shader_handles.valid())
            INIT_FAIL("get_shader_pair(debug_opaque_object)");

        wz::gpu::dx12::create_debug_opaque_context(app.ctx.device, {
            .vertex_shader = object_shader_handles.vertex,
            .pixel_shader  = object_shader_handles.pixel,
            });

        // ── Build a number of renderable scene object ──────────────────────────
        if (!build_debug_object_scene(app, kDebugObjectCount))
            INIT_FAIL("build_debug_object_scene");

        // Scalar field debug is deliberately disabled for Session 7 object rendering.
        app.scalar_debug.texture = {};
        app.scalar_debug.ready   = false;

        wz::input::init_raw_input();

        if (!build_app_jobs(app))
            INIT_FAIL("build_app_jobs");

        return true;
    }

    void update(
        wz::engine::Context& ctx,
        wz::engine::FrameContext& fctx,
        GameApp& app)
    {
        assert(app.jobs.ready);

        static float debug_anim_t = 0.0f;
        debug_anim_t += static_cast<float>(fctx.frame.delta_seconds());

        update_debug_object_animation(app, debug_anim_t);

        if (!g_logged_job_update_once)
        {
            app.ctx.logger.info("app update running through DagScheduler");
            g_logged_job_update_once = true;
        }

        AppUpdateFrameData frame_data{
            .ctx = &ctx,
            .fctx = &fctx,
            .app = &app,
            .dt = static_cast<float>(fctx.frame.delta_seconds()),
        };

        reset_frame_allocation_counters(app);

        app.jobs.exec.reset(app.jobs.graph);

        app.jobs.exec.bind(app.jobs.platform_events, &frame_data);
        app.jobs.exec.bind(app.jobs.shutdown_input, &frame_data);
        app.jobs.exec.bind(app.jobs.camera_update, &frame_data);

        app.jobs.exec.bind(app.jobs.build_view, &frame_data);
        app.jobs.exec.bind(app.jobs.compile_scene, &frame_data);
        app.jobs.exec.bind(app.jobs.build_render_ir, &frame_data);
        app.jobs.exec.bind(app.jobs.build_render_frame, &frame_data);

        app.jobs.profile.reset(fctx.frame.index);
        app.jobs.scheduler.set_profile(&app.jobs.profile);
        app.jobs.scheduler.execute(app.jobs.graph, app.jobs.exec);
        app.jobs.scheduler.set_profile(nullptr);

        log_job_profile_if_due(app, app.jobs.profile);
    }

    void render(
        GameApp& app,
        const wz::engine::FrameContext& fctx)
    {
        wz::gpu::begin_frame(app.ctx.device);
        wz::gpu::clear(app.ctx.device, 0.0f, 0.15f, 0.35f, 1.0f);

        if (app.debug_object.ready)
        {
            wz::gpu::dx12::submit_render_frame(
                app.ctx.device,
                app.frame.render_frame.frame
            );
        }

        if (app.scalar_debug.ready)
        {
            wz::gpu::dx12::ScalarFieldDebugView view{};
            view.offset_x = app.camera.x * 0.10f;
            view.offset_y = app.camera.y * 0.10f;
            view.zoom = 1.0f;

            wz::gpu::dx12::submit_scalar_field_debug_frame(
                app.ctx.device,
                view
            );
        }

        wz::gpu::end_frame(app.ctx.device);
        wz::gpu::present(app.ctx.device);
    }

    void shutdown(GameApp& app)
    {
        wz::engine::shutdown(app.ctx);
    }
}
