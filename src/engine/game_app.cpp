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

#include <cmath>

#define INIT_FAIL(msg) \
    do { OutputDebugStringA("GameApp init failed: " msg "\n"); return false; } while (0)

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
            wz::fs::Path{ "resources" }
        );

        using namespace wz::engine::assets;

        auto object_shaders = app.assets->create_shader_pair({
            .name = "debug_opaque_object",
            .vertex_path = "shaders/_debug/debug_object_vs.hlsl",
            .pixel_path = "shaders/_debug/debug_object_ps.hlsl",
            });

        if (!object_shaders.valid())
            INIT_FAIL("create_shader_pair(debug_opaque_object)");

        if (!app.assets->commit())
            INIT_FAIL("assets commit");

        app.assets->resolve_all();

        auto object_shader_handles =
            app.assets->get_shader_pair(object_shaders);

        if (!object_shader_handles.valid())
            INIT_FAIL("get_shader_pair(debug_opaque_object)");

        wz::gpu::dx12::create_debug_opaque_context(app.device, {
            .vertex_shader = object_shader_handles.vertex,
            .pixel_shader = object_shader_handles.pixel,
            });

        // ── Build one tiny renderable scene object ──────────────────────────
        {
            using namespace wz::scene;
            using namespace wz::core::graph;
            using namespace wz::math;

            SceneBuilder b;

            TransformNode root{};
            root.local = mat4_identity();

            NodeHandle root_h = add_node(b, root);

            TransformNode object{};
            object.local = mat4_identity();
            object.local.m[14] = 3.0f; // visible in front of the camera
            object.flags = TransformNodeFlag::RenderDomain;
            object.motion_type = TransformNode::MotionType::Static;

            NodeHandle object_h = add_node(b, object);

            add_edge(b, root_h, object_h);

            auto scene_result = build(std::move(b));
            if (!scene_result.has_value())
                return false;

            app.debug_object.scene = std::move(*scene_result);

            app.debug_object.descriptors.resize(
                node_count(app.debug_object.scene.polytree)
            );

            app.debug_object.descriptors[root_h] = RenderableDescriptor{
                .pipeline = RenderPipeline::None,
            };

            app.debug_object.descriptors[object_h] = RenderableDescriptor{
                .pipeline = RenderPipeline::OpaqueGeometry,
                .mesh = 0,
                .material = 0,
                .local_bounds = {},
                .splat_data = {},
                .visible = true,
            };

            propagate_all(app.debug_object.scene.polytree);

            app.debug_object.ready = true;
        }

        // Scalar field debug is deliberately disabled for Session 7 object rendering.
        app.scalar_debug.texture = {};
        app.scalar_debug.ready = false;

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

    namespace
    {
        wz::math::Mat4 perspective_dx(
            float fov_y_radians,
            float aspect,
            float near_z,
            float far_z)
        {
            using namespace wz::math;

            const float f = 1.0f / std::tan(fov_y_radians * 0.5f);

            Mat4 m{};
            m.m[0] = f / aspect;
            m.m[5] = f;
            m.m[10] = far_z / (far_z - near_z);
            m.m[11] = 1.0f;
            m.m[14] = (-near_z * far_z) / (far_z - near_z);
            return m;
        }
    }

    void render(
        GameApp& app,
        const wz::engine::FrameContext& fctx)
    {
        wz::gpu::begin_frame(app.device);
        wz::gpu::clear(app.device, 0.0f, 0.15f, 0.35f, 1.0f);

        if (app.debug_object.ready)
        {
            using namespace wz::scene;
            using namespace wz::render;
            using namespace wz::math;

            propagate_all(app.debug_object.scene.polytree);

            ViewData view{};
            //view.camera_position = Vec3{ app.camera.x, app.camera.y, 0.0f };
            view.camera_position = Vec3{ 0.0f, 0.0f, 0.0f };

            view.view = mat4_identity();
            view.view.m[12] = -app.camera.x;
            view.view.m[13] = -app.camera.y;
            view.view.m[14] = 0.0f;

            constexpr float Pi = 3.14159265358979323846f;
            const float fov = 70.0f * Pi / 180.0f;
            const float aspect = 1280.0f / 720.0f;

            view.projection = perspective_dx(fov, aspect, 0.1f, 100.0f);
            view.view_projection = mul(view.projection, view.view);

            auto compiled = compile(
                app.debug_object.scene.polytree,
                app.debug_object.descriptors,
                {},
                view
            );

            auto ir = build_render_ir(compiled.scene);
            auto frame = build_frame(ir, compiled.scene);

            wz::gpu::dx12::submit_render_frame(
                app.device,
                frame.frame
            );
        }

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
        app.assets.reset();

        if (app.device.valid())
            wz::gpu::destroy_device(app.device);

        if (app.window.valid())
            wz::window::destroy_window(app.window);

        app.device = {};
        app.window = {};
        app.initialized = false;
    }


}