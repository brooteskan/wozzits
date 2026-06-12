// src/bench/benchmark_app.cpp
#include <event/platform_event.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cassert>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

#include <bench/benchmark_app.h>
#include <math/projection.h>

#include <gpu/dx12/dx12.h>

#include <stats/scene_render_storage.h>

#define INIT_FAIL(msg) \
    do { OutputDebugStringA("BenchmarkApp init failed: " msg "\n"); return false; } while (0)

namespace wz::bench
{
    namespace
    {
        constexpr uint64_t kProfileLogEveryNFrames = 120;

        void report_device_lost_once(BenchmarkApp& app)
        {
            if (app.device_lost_reported
                || wz::gpu::device_status(app.ctx.device) != wz::gpu::DeviceStatus::Lost)
                return;

            const wz::gpu::DeviceLostInfo* info =
                wz::gpu::device_lost_info(app.ctx.device);

            app.ctx.logger.critical(
                (info && !info->message.empty())
                    ? info->message
                    : "GPU device lost; restart required.");

            app.device_lost_reported = true;
            wz::engine::shutdown();
        }

        // ─── Per-frame data forwarded into each job via JobContext ────────────────

        struct FrameData
        {
            wz::engine::Context*      ctx   = nullptr;
            wz::engine::FrameContext* fctx  = nullptr;
            BenchmarkApp*             app   = nullptr;
            float                     dt    = 0.0f;
        };

        // ─── Navigation cursor management ────────────────────────────────────────

        void apply_navigation_cursor(BenchmarkApp& app)
        {
            HWND hwnd = static_cast<HWND>(
                wz::window::get_native_handle(app.ctx.window));

            if (app.navigation_active)
            {
                ShowCursor(FALSE);

                RECT client{};
                GetClientRect(hwnd, &client);
                POINT tl{ client.left, client.top };
                POINT br{ client.right, client.bottom };
                ClientToScreen(hwnd, &tl);
                ClientToScreen(hwnd, &br);
                RECT clip{ tl.x, tl.y, br.x, br.y };
                ClipCursor(&clip);
            }
            else
            {
                ShowCursor(TRUE);
                ClipCursor(nullptr);
            }
        }

        // ─── View matrix from flying camera ──────────────────────────────────────

        wz::scene::ViewData build_view(
            const FlyingCamera&             cam,
            const wz::engine::FrameContext& fctx)
        {
            using namespace wz::math;

            const CameraBasis b = camera_basis(cam);

            const float rx = b.right.x,   ry = b.right.y,   rz = b.right.z;
            const float ux = b.up.x,      uy = b.up.y,      uz = b.up.z;
            const float fx = b.forward.x,  fy = b.forward.y, fz = b.forward.z;

            const float px = cam.x, py = cam.y, pz = cam.z;

            Mat4 V{};
            // Column-major, column-vector convention (clip = VP * world_pos).
            // Each ROW of the 3×3 block is one basis vector so that
            // mul_point(V, p) produces (dot(p−cam, right), dot(p−cam, up), dot(p−cam, fwd)).
            V.m[0]  = rx;    V.m[1]  = ux;    V.m[2]  = fx;    V.m[3]  = 0.0f;
            V.m[4]  = ry;    V.m[5]  = uy;    V.m[6]  = fy;    V.m[7]  = 0.0f;
            V.m[8]  = rz;    V.m[9]  = uz;    V.m[10] = fz;    V.m[11] = 0.0f;
            V.m[12] = -(rx * px + ry * py + rz * pz);
            V.m[13] = -(ux * px + uy * py + uz * pz);
            V.m[14] = -(fx * px + fy * py + fz * pz);
            V.m[15] = 1.0f;

            constexpr float kPi  = 3.14159265358979323846f;
            const float fov      = 70.0f * kPi / 180.0f;
            const int   win_w    = fctx.input.window.width;
            const int   win_h    = fctx.input.window.height;
            const float aspect   =
                (win_w > 0 && win_h > 0)
                ? static_cast<float>(win_w) / static_cast<float>(win_h)
                : 16.0f / 9.0f;

            wz::scene::ViewData view{};
            view.camera_position = Vec3{ px, py, pz };
            view.view            = V;
            view.projection      = wz::math::projection_perspective_dx(fov, aspect, 0.1f, 1000.0f);
            view.view_projection = mul(view.projection, view.view);

            return view;
        }

        // ─── Timing helpers ───────────────────────────────────────────────────────

        double ticks_to_ms(uint64_t ticks)
        {
            const double tps = static_cast<double>(
                wz::time::TimeSource::ticks_per_second());
            return static_cast<double>(ticks) * 1000.0 / tps;
        }

        void log_profile_if_due(BenchmarkApp& app, const wz::jobs::FrameJobProfile& profile)
        {
            if (profile.timings.empty())
                return;
            if ((profile.frame_index % kProfileLogEveryNFrames) != 0)
                return;

            uint64_t total_ticks       = 0;
            uint64_t prep_ticks        = 0;
            const wz::jobs::JobTimingRecord* slowest = nullptr;

            for (const auto& rec : profile.timings)
            {
                const uint64_t dt = rec.duration_ticks();
                total_ticks += dt;

                const char* name = rec.name ? rec.name : "";
                if (   std::strcmp(name, "build_view")        == 0
                    || std::strcmp(name, "compile_scene")     == 0
                    || std::strcmp(name, "build_render_ir")   == 0
                    || std::strcmp(name, "build_render_frame")== 0)
                {
                    prep_ticks += dt;
                }

                if (!slowest || dt > slowest->duration_ticks())
                    slowest = &rec;
            }

            const auto& culling = app.frame.render_ir.ir.culling;
            const uint64_t cmd_count =
                  app.frame.render_frame.frame.opaque.size()
                + app.frame.render_frame.frame.splats.size()
                + app.frame.render_frame.frame.transparent.size()
                + app.frame.render_frame.frame.particles.size();

            std::ostringstream summary;
            summary << std::fixed << std::setprecision(3);
            summary
                << "bench frame=" << profile.frame_index
                << " scene=" << app.scene_def.name
                << " path=" << wz::engine::render_prep_path_name(app.frame_dirty.render_prep_path())
                << " nav=" << (app.navigation_active ? "on" : "off")
                << " total=" << ticks_to_ms(total_ticks) << " ms"
                << " prep=" << ticks_to_ms(prep_ticks)   << " ms"
                << " cmds=" << cmd_count
                << " vis_opaque=" << culling.visible_opaque
                << " cull_opaque=" << culling.culled_opaque;

            if (slowest)
            {
                summary << " slowest="
                        << (slowest->name ? slowest->name : "<unnamed>")
                        << " " << ticks_to_ms(slowest->duration_ticks()) << " ms";
            }

            app.ctx.logger.info(summary.str());
        }

        // ─── Jobs ─────────────────────────────────────────────────────────────────

        void job_platform_events(wz::jobs::JobContext& ctx)
        {
            auto* d = static_cast<FrameData*>(ctx.frame_user);
            wz::window::pump_messages();

            PlatformEvent event{};
            while (wz::window::poll_event(d->app->ctx.window, event))
            {
                switch (event.type)
                {
                case PlatformEvent::Type::Close:
                    d->ctx->running = false;
                    break;
                case PlatformEvent::Type::Resize:
                    if (event.resize.width > 0 && event.resize.height > 0)
                    {
                        if (!wz::gpu::resize(
                                d->app->ctx.device,
                                event.resize.width,
                                event.resize.height))
                        {
                            report_device_lost_once(*d->app);
                        }
                    }
                    break;
                default:
                    break;
                }
            }

            if (wz::window::window_should_close(d->app->ctx.window))
                d->ctx->running = false;
        }

        void job_shutdown_input(wz::jobs::JobContext& ctx)
        {
            auto* d = static_cast<FrameData*>(ctx.frame_user);

            // Only allow Backspace-to-quit while scene navigation is active.
            // When navigation is off the user is interacting with ImGui panels
            // and stray key presses should not kill the app.
            if (d->app->navigation_active
                && d->fctx->input.keyboard.pressed[VK_BACK])
                d->ctx->running = false;
        }

        void job_camera_update(wz::jobs::JobContext& ctx)
        {
            auto* d = static_cast<FrameData*>(ctx.frame_user);
            BenchmarkApp& app = *d->app;

            // ESC pressed: toggle navigation mode.
            if (d->fctx->input.keyboard.pressed[VK_ESCAPE])
            {
                app.navigation_active = !app.navigation_active;
                apply_navigation_cursor(app);
            }

            if (app.navigation_active)
                update_flying_camera(app.camera, d->fctx->input, d->dt);
        }

        void job_build_view(wz::jobs::JobContext& ctx)
        {
            auto* d = static_cast<FrameData*>(ctx.frame_user);
            d->app->frame.view = build_view(d->app->camera, *d->fctx);
        }

        void job_compile_scene(wz::jobs::JobContext& ctx)
        {
            auto* d = static_cast<FrameData*>(ctx.frame_user);
            BenchmarkApp& app = *d->app;
            SceneRuntime& sr  = app.scene_runtime;

            if (!sr.ready)
                return;

            // Scene callback fills dirty_nodes each frame.
            sr.dirty_nodes.clear();
            if (app.scene_def.update)
                app.scene_def.update(sr, d->dt);

            sr.transforms_dirty = !sr.dirty_nodes.empty();

            if (!sr.compiled_scene_valid)
            {
                wz::scene::compile(
                    app.frame.compiled_scene,
                    sr.scene.polytree,
                    sr.descriptors,
                    {},
                    app.frame.view);

                if (!sr.first_compile_logged)
                {
                    const auto& compiled = app.frame.compiled_scene.scene;
                    std::ostringstream msg;
                    msg
                        << "[BenchmarkApp] scene compiler full compile"
                        << " scene=" << app.scene_def.name
                        << " runtime=" << static_cast<const void*>(&sr)
                        << " scene_storage=" << static_cast<const void*>(&sr.scene)
                        << " runtime_nodes="
                        << wz::core::graph::node_count(sr.scene.polytree)
                        << " descriptor_slots=" << sr.descriptors.size()
                        << " opaque=" << compiled.opaque.size()
                        << " transparent=" << compiled.transparent.size()
                        << " splats=" << compiled.splats.size()
                        << " particles=" << compiled.particles.size()
                        << " lights=" << compiled.lights.size();
                    app.ctx.logger.info(msg.str());
                    sr.first_compile_logged = true;
                }

                sr.compiled_scene_valid = true;
                sr.transforms_dirty     = false;
                app.frame_dirty.mark_render_full_compile();
                return;
            }

            if (sr.transforms_dirty)
            {
                // Update world matrices for dirty nodes (caller is responsible for
                // propagating the polytree before calling update_compiled_transforms).
                // The scene_def.update callback must have already written new locals
                // and the scene graph must be up to date.
                wz::scene::update_compiled_transforms(
                    app.frame.compiled_scene,
                    sr.scene.polytree,
                    sr.descriptors,
                    app.frame.view,
                    sr.dirty_nodes,
                    true);

                sr.transforms_dirty        = false;
                app.frame_dirty.mark_render_transform_and_view();
                return;
            }

            wz::scene::update_view(app.frame.compiled_scene, app.frame.view);
            app.frame_dirty.mark_render_view_only();
        }

        void job_build_render_ir(wz::jobs::JobContext& ctx)
        {
            auto* d = static_cast<FrameData*>(ctx.frame_user);
            BenchmarkApp& app = *d->app;

            if (!app.scene_runtime.ready)
                return;

            if (app.frame_dirty.render_prep_path() == RenderPrepPath::FullCompile)
                wz::render::build_render_ir(app.frame.render_ir, app.frame.compiled_scene.scene);
            else
                wz::render::update_render_ir(app.frame.render_ir, app.frame.compiled_scene.scene);
        }

        void job_build_render_frame(wz::jobs::JobContext& ctx)
        {
            auto* d = static_cast<FrameData*>(ctx.frame_user);
            BenchmarkApp& app = *d->app;

            if (!app.scene_runtime.ready)
                return;

            if (app.frame_dirty.render_prep_path() == RenderPrepPath::FullCompile)
            {
                wz::render::build_frame(
                    app.frame.render_frame,
                    app.frame.render_ir.ir,
                    app.frame.compiled_scene.scene);
            }
            else
            {
                wz::render::update_frame_view(
                    app.frame.render_frame,
                    app.frame.render_ir.ir,
                    app.frame.compiled_scene.scene);
            }
        }

        // ─── Job graph assembly ───────────────────────────────────────────────────

        bool build_jobs(BenchmarkApp& app)
        {
            auto& j = app.jobs;

            j.platform_events   = j.graph.add_job({ .name = "platform_events",   .lane = wz::jobs::ExecutionLane::MainThread, .run = job_platform_events   });
            j.shutdown_input    = j.graph.add_job({ .name = "shutdown_input",    .lane = wz::jobs::ExecutionLane::MainThread, .run = job_shutdown_input    });
            j.camera_update     = j.graph.add_job({ .name = "camera_update",     .lane = wz::jobs::ExecutionLane::MainThread, .run = job_camera_update     });
            j.build_view        = j.graph.add_job({ .name = "build_view",        .lane = wz::jobs::ExecutionLane::MainThread, .run = job_build_view        });
            j.compile_scene     = j.graph.add_job({ .name = "compile_scene",     .lane = wz::jobs::ExecutionLane::MainThread, .run = job_compile_scene     });
            j.build_render_ir   = j.graph.add_job({ .name = "build_render_ir",   .lane = wz::jobs::ExecutionLane::MainThread, .run = job_build_render_ir   });
            j.build_render_frame= j.graph.add_job({ .name = "build_render_frame",.lane = wz::jobs::ExecutionLane::MainThread, .run = job_build_render_frame});

            j.graph.add_dependency(j.platform_events,    j.shutdown_input);
            j.graph.add_dependency(j.shutdown_input,     j.camera_update);
            j.graph.add_dependency(j.camera_update,      j.build_view);
            j.graph.add_dependency(j.build_view,         j.compile_scene);
            j.graph.add_dependency(j.compile_scene,      j.build_render_ir);
            j.graph.add_dependency(j.build_render_ir,    j.build_render_frame);

            j.ready = j.graph.commit();
            return j.ready;
        }

    } // anonymous namespace

    // ─── Public API ───────────────────────────────────────────────────────────────

    bool init(BenchmarkApp& app, SceneDefinition scene_def)
    {
        if (!wz::engine::init(app.ctx, {
                .window = {
                    .title  = "Wozzits Benchmark",
                    .width  = 1280,
                    .height = 720,
                },
                .resource_root = "resources",
            }))
            return false;

        using namespace wz::engine::assets;

        auto object_shaders = app.ctx.assets->shaders().create_shader_pair({
            .name        = "debug_opaque_object",
            .vertex_path = "shaders/_debug/debug_object_vs.hlsl",
            .pixel_path  = "shaders/_debug/debug_object_ps.hlsl",
            });

        if (!object_shaders.valid())
            INIT_FAIL("create_shader_pair(debug_opaque_object)");

        // Store the scene definition now so pre_commit can be invoked while
        // the asset registration phase is still open.
        app.scene_def = std::move(scene_def);
        app.ctx.logger.info(
            std::string("[BenchmarkApp] scene definition received: ")
            + app.scene_def.name);

        if (app.scene_def.pre_commit)
        {
            app.ctx.logger.info(
                std::string("[BenchmarkApp] pre_commit begin: ")
                + app.scene_def.name);
            if (!app.scene_def.pre_commit(*app.ctx.assets))
                INIT_FAIL("SceneDefinition::pre_commit returned false");
            app.ctx.logger.info(
                std::string("[BenchmarkApp] pre_commit complete: ")
                + app.scene_def.name);
        }

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

        if (!app.scene_def.build)
            INIT_FAIL("SceneDefinition has no build callback");

        app.ctx.logger.info(
            std::string("[BenchmarkApp] runtime scene build begin: ")
            + app.scene_def.name);
        if (!app.scene_def.build(app.scene_runtime, app.ctx))
            INIT_FAIL("SceneDefinition::build returned false");
        {
            std::ostringstream msg;
            msg
                << "[BenchmarkApp] runtime scene build complete"
                << " scene=" << app.scene_def.name
                << " app=" << static_cast<const void*>(&app)
                << " runtime=" << static_cast<const void*>(&app.scene_runtime)
                << " scene_storage="
                << static_cast<const void*>(&app.scene_runtime.scene)
                << " runtime_nodes="
                << wz::core::graph::node_count(app.scene_runtime.scene.polytree)
                << " descriptor_slots=" << app.scene_runtime.descriptors.size();
            app.ctx.logger.info(msg.str());
        }

        app.scene_runtime.ready = true;

        wz::input::init_raw_input();

        if (!build_jobs(app))
            INIT_FAIL("build_jobs");

        app.ctx.logger.info(
            std::string("[BenchmarkApp] ready: scene=") + app.scene_def.name +
            " press ESC to toggle navigation, BKSPC to quit");

        return true;
    }

    void update(
        wz::engine::Context&      ctx,
        wz::engine::FrameContext& fctx,
        BenchmarkApp&             app)
    {
        assert(app.jobs.ready);

        FrameData fd{
            .ctx  = &ctx,
            .fctx = &fctx,
            .app  = &app,
            .dt   = static_cast<float>(fctx.frame.delta_seconds()),
        };

        app.frame.compiled_scene.stable_stats.reset_build_counters();
        app.frame.compiled_scene.view_stats.reset_build_counters();
        app.frame.compiled_scene.metadata_stats.reset_build_counters();
        app.frame.render_ir.stats.reset_build_counters();
        app.frame.render_frame.stable_stats.reset_build_counters();
        app.frame.render_frame.view_stats.reset_build_counters();

        app.jobs.exec.reset(app.jobs.graph);
        app.jobs.exec.bind(app.jobs.platform_events,   &fd);
        app.jobs.exec.bind(app.jobs.shutdown_input,    &fd);
        app.jobs.exec.bind(app.jobs.camera_update,     &fd);
        app.jobs.exec.bind(app.jobs.build_view,        &fd);
        app.jobs.exec.bind(app.jobs.compile_scene,     &fd);
        app.jobs.exec.bind(app.jobs.build_render_ir,   &fd);
        app.jobs.exec.bind(app.jobs.build_render_frame,&fd);

        app.jobs.profile.reset(fctx.frame.index);
        app.jobs.scheduler.set_profile(&app.jobs.profile);
        app.jobs.scheduler.execute(app.jobs.graph, app.jobs.exec);
        app.jobs.scheduler.set_profile(nullptr);

        // log_profile_if_due(app, app.jobs.profile);
    }

    void render_contents(BenchmarkApp& app, const wz::engine::FrameContext& /*fctx*/)
    {
        if (app.scene_runtime.ready)
        {
            wz::gpu::dx12::submit_render_frame(
                app.ctx.device,
                app.frame.render_frame.frame);
        }
    }

    void render(BenchmarkApp& app, const wz::engine::FrameContext& fctx)
    {
        if (!wz::gpu::begin_frame(app.ctx.device))
        {
            report_device_lost_once(app);
            return;
        }
        wz::gpu::clear(app.ctx.device, 0.05f, 0.05f, 0.1f, 1.0f);
        render_contents(app, fctx);
        if (!wz::gpu::end_frame(app.ctx.device))
        {
            report_device_lost_once(app);
            return;
        }
        if (!wz::gpu::present(app.ctx.device))
            report_device_lost_once(app);
    }

    void shutdown(BenchmarkApp& app)
    {
        if (app.navigation_active)
        {
            ShowCursor(TRUE);
            ClipCursor(nullptr);
        }
        wz::engine::shutdown(app.ctx);
    }
}
