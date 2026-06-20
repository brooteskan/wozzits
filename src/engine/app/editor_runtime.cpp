// src/engine/app/editor_runtime.cpp

#include <engine/app/editor_runtime.h>

#include <engine/app/wozzits_app_v1.h>
#include <engine/app_context.h>

#include <gpu/gpu.h>
#include <window/window2.h>

namespace wz::app
{
    int run_project_runtime(
        const std::string& window_title,
        const wz::fs::Path& asset_graph,
        const wz::fs::Path& scene,
        const wz::fs::Path& resource_root,
        const std::function<bool()>& should_stop)
    {
        wz::engine::AppContext ctx;

        wz::engine::AppDesc desc;
        desc.window = { window_title.c_str(), 1280, 720, true, false };
        desc.resource_root = resource_root;

        if (!wz::engine::init(ctx, desc)) {
            return 1;  // init logged the failure
        }

        {
            wz::app::WozzitsApp_v1 app(ctx);

            // Present one cleared frame before the (possibly multi-second) asset
            // compile, so the window shows the background instead of an
            // uninitialized backbuffer while load_scene blocks.
            if (wz::gpu::begin_frame(ctx.device)) {
                wz::gpu::clear(ctx.device, 0.10f, 0.10f, 0.12f, 1.0f);
                wz::gpu::end_frame(ctx.device);
                wz::gpu::present(ctx.device);
            }

            if (!app.load_scene(wz::app::WozzitsAppSceneLoadDesc{
                    .asset_graph = asset_graph,
                    .scene = scene,
                }))
            {
                ctx.logger.error("load scene failed");
            }

            bool running = true;
            while (running
                && !wz::window::window_should_close(ctx.window)
                && !(should_stop && should_stop()))
            {
                wz::window::pump_messages();

                PlatformEvent event{};
                while (wz::window::poll_event(ctx.window, event)) {
                    if (event.type == PlatformEvent::Type::Close) {
                        running = false;
                        break;
                    }
                    if (event.type == PlatformEvent::Type::Resize) {
                        wz::gpu::resize(
                            ctx.device,
                            event.resize.width,
                            event.resize.height);
                    }
                }
                if (!running) {
                    break;
                }

                app.simulation_tick();

                if (!wz::gpu::begin_frame(ctx.device)) {
                    ctx.logger.error("begin_frame failed");
                    break;
                }
                wz::gpu::clear(ctx.device, 0.10f, 0.10f, 0.12f, 1.0f);
                if (!app.render_scene()) {
                    break;
                }
                if (!wz::gpu::end_frame(ctx.device)) {
                    ctx.logger.error("end_frame failed");
                    break;
                }
                if (!wz::gpu::present(ctx.device)) {
                    ctx.logger.error("present failed");
                    break;
                }
            }
        }

        wz::engine::shutdown(ctx);
        return 0;
    }
}
