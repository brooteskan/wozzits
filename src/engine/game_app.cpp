// src/engine/game_app.cpp
#include <event/platform_event.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <engine/game_app.h>

#include <gpu/scalar_field_texture.h>
#include <gpu/dx12/dx12.h>
#include <engine/assets/scalar_field/scalar_field.h>

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

        using namespace wz::engine::assets;

        ScalarFieldAsset field = app.assets->create_procedural_scalar_field({
            .name = "debug/gradient_x",
            .width = 256,
            .height = 256,
            .depth = 1,
            .generator = ScalarFieldGenerator::GradientX,
            .frequency = 1.0f,
            .amplitude = 1.0f,
            .format = ScalarFieldFormat::Float32,
            .domain_kind = ScalarFieldDomainKind::Spatial2D,
            });

        if (!field.valid())
            return false;

        auto shaders = app.assets->create_shader_pair({
            .name = "scalar_field_debug",
            .vertex_path = "shaders/scalar_field/scalar_field_vs.hlsl",
            .pixel_path = "shaders/scalar_field/scalar_field_ps.hlsl",
            });

        if (!shaders.valid())
            return false;

        if (!app.assets->commit())
            return false;

        app.assets->resolve_all();

        auto shader_handles = app.assets->get_shader_pair(shaders);
        if (!shader_handles.valid())
            return false;

        ScalarFieldHandle scalar_handle = app.assets->get_scalar_field(field);
        if (!scalar_handle.valid())
            return false;

        const ScalarFieldData* scalar_data =
            app.assets->get_scalar_field_data(scalar_handle);

        if (!scalar_data || !scalar_data->valid())
            return false;

        wz::gpu::GPUHandle texture =
            wz::gpu::upload_scalar_field_texture(app.device, *scalar_data);

        if (!texture.valid())
            return false;

        wz::gpu::dx12::create_scalar_field_debug_context(app.device, {
            .vertex_shader = shader_handles.vertex,
            .pixel_shader = shader_handles.pixel,
            .scalar_field_texture = texture,
            .display_min = scalar_data->min_value,
            .display_max = scalar_data->max_value,
            .normalize_for_display = true,
            });

        app.scalar_debug.texture = texture;
        app.scalar_debug.ready = true;

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
        wz::gpu::begin_frame(app.device);
        wz::gpu::clear(app.device, 0.05f, 0.05f, 0.05f, 1.0f);

        if (app.scalar_debug.ready)
        {
            wz::gpu::dx12::ScalarFieldDebugView view{};
            view.offset_x = app.camera.x * 0.10f;
            view.offset_y = app.camera.y * 0.10f;
            //view.offset_x = 0.50f;
            //view.offset_y = 0.0f;
            view.zoom = 1.0f;

            wz::gpu::dx12::submit_scalar_field_debug_frame(
                app.device,
                view
            );
        }

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