// src/engine/game_app.cpp
#include <event/platform_event.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <engine/game_app.h>
#include <engine/runtime_camera.h>
#include <math/projection.h>

#include <gpu/scalar_field_texture.h>
#include <gpu/dx12/dx12.h>
#include <engine/assets/scalar_field/scalar_field.h>

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

        auto object_shaders = app.assets->shaders().create_shader_pair({
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
            app.assets->shaders().get_shader_pair(object_shaders);

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
            app.debug_object.transforms_dirty = false;
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

        const auto& input = fctx.input;

        if (input.keyboard.pressed[VK_ESCAPE])
            ctx.running = false;

        const float dt = static_cast<float>(fctx.frame.delta_seconds());

        update_camera(app.camera, input, dt);
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

            if (app.debug_object.transforms_dirty)
            {
                propagate_all(app.debug_object.scene.polytree);
                app.debug_object.transforms_dirty = false;
            }

            ViewData view{};
            //view.camera_position = Vec3{ app.camera.x, app.camera.y, 0.0f };
            view.camera_position = Vec3{ 0.0f, 0.0f, 0.0f };

            view.view = mat4_identity();
            view.view.m[12] = -app.camera.x;
            view.view.m[13] = -app.camera.y;
            view.view.m[14] = 0.0f;

            constexpr float Pi = 3.14159265358979323846f;
            const float fov = 70.0f * Pi / 180.0f;

            const int win_w = fctx.input.window.width;
            const int win_h = fctx.input.window.height;
            const float aspect = (win_w > 0 && win_h > 0)
                ? static_cast<float>(win_w) / static_cast<float>(win_h)
                : 1280.0f / 720.0f;

            view.projection = wz::math::projection_perspective_dx(fov, aspect, 0.1f, 100.0f);
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