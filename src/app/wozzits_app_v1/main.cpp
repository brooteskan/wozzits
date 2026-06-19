// src/app/wozzits_app_v1/main.cpp
//
// wozzits_app_v1 — the thin runtime driver (Atom "Bootstrap" role). It owns the
// engine AppContext and the while-loop + device-frame boundaries, and composes
// a WozzitsApp_v1 (the base render-app). This is the compile-once runtime: load
// a project, then render. The editor is a different driver over the same
// WozzitsApp_v1 (its own imgui loop + draft editing), not a subclass.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "wozzits_app_v1.h"

#include <engine/app_context.h>

#include <gpu/gpu.h>
#include <window/window2.h>

int main(int /*argc*/, char** /*argv*/)
{
    wz::engine::AppContext ctx;

    wz::engine::AppDesc desc;
    desc.window = { "Wozzits App v1", 1280, 720, true, false };

    if (!wz::engine::init(ctx, desc)) {
        return 1;  // init logged the failure
    }

    {
        wz::app::WozzitsApp_v1 app(ctx);

        // Present one cleared frame BEFORE the (possibly multi-second) asset
        // compile, so the window shows the background instead of an
        // uninitialized white backbuffer while load_project blocks.
        if (wz::gpu::begin_frame(ctx.device)) {
            wz::gpu::clear(ctx.device, 0.10f, 0.10f, 0.12f, 1.0f);
            wz::gpu::end_frame(ctx.device);
            wz::gpu::present(ctx.device);
        }

        // Compile-once: load the project's scene + asset graph and bind it.
        // Path is resource-root-relative (the asset library prefixes "resources/").
        app.load_project(
            "projects/test_mesh_001/test_mesh_001.project.json");

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
