// src/engine/game_app.cpp
#include <event/platform_event.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <engine/game_app.h>


namespace wz::app
{
    bool init(GameApp& app)
    {
        app.logger.set_callback(wz::LogSinkType::Stderr);

        app.window = wz::window::create_window({
            .title = "Wozzits Runtime",
            .width = 1280,
            .height = 720,
            // .resizable = true
            });

        if (!app.window.valid())
            return false;

        app.device = wz::gpu::create_device(app.window);

        if (!app.device.valid())
        {
            wz::window::destroy_window(app.window);
            app.window = {};
            return false;
        }

        app.assets = std::make_unique<wz::engine::assets::EngineAssetLibrary>(
            app.device,
            app.logger,
            wz::fs::Path{"resources" }
        );

        wz::input::init_raw_input();

        app.initialized = true;
        return true;
    }

    void update(
        wz::engine::Context& ctx,
        wz::engine::FrameContext& fctx,
        GameApp& app)
    {
        wz::window::pump_messages();

        PlatformEvent event{};
        while (wz::window::poll_event(app.window, event))
        {
            switch (event.type)
            {
            case PlatformEvent::Type::Close:
                ctx.running = false;
                break;

            case PlatformEvent::Type::Resize:
                if (event.resize.width > 0 && event.resize.height > 0)
                {
                    wz::gpu::resize(
                        app.device,
                        event.resize.width,
                        event.resize.height
                    );
                }
                break;

            default:
                break;
            }
        }

        if (wz::window::window_should_close(app.window))
            ctx.running = false;

        // camera input below
        const auto& input = fctx.input;

        if (input.keyboard.pressed[VK_ESCAPE])
            ctx.running = false;

        const float dt_ticks =
            static_cast<float>(fctx.frame.interval.end - fctx.frame.interval.start);

        const float dt =
            static_cast<float>(fctx.frame.delta_seconds());

        const float move = app.camera.move_speed * dt;

        if (input.keyboard.down['W'])
            app.camera.y += move;

        if (input.keyboard.down['S'])
            app.camera.y -= move;

        if (input.keyboard.down['A'])
            app.camera.x -= move;

        if (input.keyboard.down['D'])
            app.camera.x += move;

        if (input.keyboard.down[VK_UP])
            app.camera.pitch += move;

        if (input.keyboard.down[VK_DOWN])
            app.camera.pitch -= move;

        if (input.keyboard.down[VK_LEFT])
            app.camera.yaw -= move;

        if (input.keyboard.down[VK_RIGHT])
            app.camera.yaw += move;

        app.camera.yaw +=
            static_cast<float>(input.mouse.dx) * app.camera.look_speed;

        app.camera.pitch +=
            static_cast<float>(input.mouse.dy) * app.camera.look_speed;

        app.camera.pitch =
            std::clamp(app.camera.pitch, -1.5f, 1.5f);
    }

    void render(
        GameApp& app,
        const wz::engine::FrameContext& fctx)
    {
        const float r = 0.05f + 0.05f * app.camera.x;
        const float g = 0.07f + 0.05f * app.camera.y;

        wz::gpu::begin_frame(app.device);
        wz::gpu::clear(app.device, r, g, 0.10f, 1.0f);
        wz::gpu::end_frame(app.device);
        wz::gpu::present(app.device);
    }

    void shutdown(GameApp& app)
    {
        if (!app.initialized)
            return;

        app.assets.reset();


        wz::gpu::destroy_device(app.device);
        wz::window::destroy_window(app.window);

        app.device = {};
        app.window = {};
        app.initialized = false;
    }


}