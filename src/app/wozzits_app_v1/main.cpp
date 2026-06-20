// src/app/wozzits_app_v1/main.cpp
//
// wozzits_app_v1 — the thin runtime driver (Atom "Bootstrap" role). It owns the
// engine AppContext and the while-loop + device-frame boundaries, and composes
// a WozzitsApp_v1 (the base render-app). This is the compile-once runtime:
// receive explicit runtime paths, then render. The editor is a different driver
// over the same WozzitsApp_v1 (its own imgui loop + draft editing), not a
// subclass.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <engine/app/wozzits_app_v1.h>

#include <engine/app_context.h>

#include <gpu/gpu.h>
#include <window/window2.h>

#include <iostream>
#include <string>

namespace
{
    struct RuntimeOptions
    {
        wz::fs::Path resource_root{ "resources" };
        wz::fs::Path asset_graph;
        wz::fs::Path scene;
    };

    bool parse_options(
        int argc,
        char** argv,
        RuntimeOptions& out,
        std::string& error)
    {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--resource-root" && i + 1 < argc) {
                out.resource_root = argv[++i];
            }
            else if (arg == "--asset-graph" && i + 1 < argc) {
                out.asset_graph = argv[++i];
            }
            else if (arg == "--scene" && i + 1 < argc) {
                out.scene = argv[++i];
            }
            else {
                error = "unknown or incomplete option: " + arg;
                return false;
            }
        }

        if (out.asset_graph.empty()) {
            error = "missing required option: --asset-graph <path>";
            return false;
        }
        if (out.scene.empty()) {
            error = "missing required option: --scene <path>";
            return false;
        }
        return true;
    }
}

int main(int argc, char** argv)
{
    RuntimeOptions options;
    std::string error;
    if (!parse_options(argc, argv, options, error)) {
        std::cerr
            << "usage: wozzits_app_v1 --asset-graph <path> --scene <path> "
               "[--resource-root <path>]\n"
            << error << '\n';
        return 2;
    }

    wz::engine::AppContext ctx;

    wz::engine::AppDesc desc;
    desc.window = { "Wozzits App v1", 1280, 720, true, false };
    desc.resource_root = options.resource_root;

    if (!wz::engine::init(ctx, desc)) {
        return 1;  // init logged the failure
    }

    {
        wz::app::WozzitsApp_v1 app(ctx);

        // Present one cleared frame BEFORE the (possibly multi-second) asset
        // compile, so the window shows the background instead of an
        // uninitialized white backbuffer while load_scene blocks.
        if (wz::gpu::begin_frame(ctx.device)) {
            wz::gpu::clear(ctx.device, 0.10f, 0.10f, 0.12f, 1.0f);
            wz::gpu::end_frame(ctx.device);
            wz::gpu::present(ctx.device);
        }

        ctx.logger.info("runtime asset graph: " + options.asset_graph);
        ctx.logger.info("runtime scene: " + options.scene);

        if (!app.load_scene(wz::app::WozzitsAppSceneLoadDesc{
                .asset_graph = options.asset_graph,
                .scene = options.scene,
            }))
        {
            ctx.logger.error("load scene failed");
        }

        bool running = true;
        while (running && !wz::window::window_should_close(ctx.window)) {
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

            // The driver owns the loop + device frame; the app owns sim + draws.
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
