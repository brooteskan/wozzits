#include <iostream>

#include <window/window2.h>
#include <event/platform_event.h>
#include <input/input.h>

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>
#include <gpu/dx12/dx12.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/scalar_field/scalar_field.h>
#include <engine/assets/scalar_field_asset_module.h>
#include <engine/assets/renderable_asset_module.h>

#include <engine/rendering/builtin_render_programs.h>
#include <engine/rendering/renderable_gpu_cache.h>
#include <engine/rendering/renderable_debug_runtime.h>

#include <logging/logger.h>
#include <file/filesystem.h>

#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>

int main()
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF);

    _CrtMemState mem_start;
    _CrtMemState mem_end;
    _CrtMemState mem_diff;

    _CrtMemCheckpoint(&mem_start);

    {
        wz::Logger logger;
        wz::logging::init_logger(logger, {});

        wz::window::WindowDesc desc;
        desc.title = "Wozzits Scalar Field Debug";
        desc.width = 800;
        desc.height = 600;

        wz::window::WindowHandle window = wz::window::create_window(desc);
        if (!window.valid())
            return 1;

        wz::gpu::Device device = wz::gpu::create_device(window);
        if (!device.valid())
            return 1;

        wz::input::init_raw_input();

        wz::engine::assets::EngineAssetLibrary assets{
            device,
            logger,
            wz::fs::Path{ "resources" }
        };

        using namespace wz::engine::assets;

        // ── procedural scalar field asset ─────────────────────────────────
        ScalarFieldAsset scalar_field_asset =
            assets.scalar_fields().create_procedural_scalar_field({
                .name = "debug/procedural_scalar_field",
                .width = 512,
                .height = 512,
                .depth = 1,
                .generator = ScalarFieldGenerator::RadialGradient,
                .frequency = 8.0f,
                .amplitude = 1.0f,
                .format = ScalarFieldFormat::Float32,
                .domain_kind = ScalarFieldDomainKind::Spatial2D,
                });

        if (!scalar_field_asset.valid())
            return 1;

        // ── renderable asset from scalar field asset ──────────────────────
        RenderableAsset renderable_asset =
            assets.renderables().create_scalar_field_debug({
                .name = "debug/procedural_scalar_field_debug_renderable",
                .scalar_field = scalar_field_asset,
                });

        if (!renderable_asset.valid())
            return 1;

        // ── shader pair from built-in render program ──────────────────────
        ShaderPairDesc shader_desc{};

        if (!wz::engine::rendering::get_builtin_shader_pair_desc(
            BuiltinRenderProgram::ScalarFieldDebug,
            shader_desc))
        {
            return 1;
        }

        ShaderPairAsset shaders =
            assets.shaders().create_shader_pair(shader_desc);

        if (!shaders.valid())
            return 1;

        // ── resolve assets ────────────────────────────────────────────────
        if (!assets.commit())
            return 1;

        const auto report = assets.resolve_all();
        if (!report.ok())
            return 1;

        // ── setup renderable debug context ────────────────────────────────
        wz::engine::rendering::RenderableGpuCache renderable_cache;
        wz::engine::rendering::DebugRenderableSetup debug_setup{};

        if (!wz::engine::rendering::setup_debug_renderable_context(
            device,
            assets,
            renderable_cache,
            renderable_asset,
            shaders,
            debug_setup))
        {
            return 1;
        }

        const wz::engine::rendering::PreparedRenderable& prepared =
            debug_setup.prepared;

        if (prepared.kind != RenderableKind::ScalarField)
            return 1;

        if (prepared.gpu_resource.type != wz::gpu::GPUResourceType::Texture)
            return 1;

        if (prepared.program != BuiltinRenderProgram::ScalarFieldDebug)
            return 1;

        // ── frame state ───────────────────────────────────────────────────
        float offset_x = 0.0f;
        float offset_y = 0.0f;
        float zoom = 1.0f;

        float display_min = 0.0f;
        float display_max = 1.0f;
        bool normalize_for_display = true;

        // ── frame loop ────────────────────────────────────────────────────
        while (!wz::window::window_should_close(window))
        {
            wz::window::pump_messages();

            PlatformEvent event{};
            while (wz::window::poll_event(window, event))
            {
                if (event.type == PlatformEvent::Type::Resize)
                {
                    wz::gpu::resize(
                        device,
                        event.resize.width,
                        event.resize.height);
                }

                if (event.type == PlatformEvent::Type::Close)
                {
                    std::cout << "Close event\n";
                }

                // Keep input handling simple here. If your scalar-field window
                // already has keyboard/mouse pan/zoom, preserve that code and
                // just write the values into ScalarFieldDebugView below.
            }

            wz::gpu::begin_frame(device);
            wz::gpu::clear(device, 0.05f, 0.05f, 0.05f, 1.0f);

            wz::gpu::dx12::ScalarFieldDebugView view{};
            view.offset_x = offset_x;
            view.offset_y = offset_y;
            view.zoom = zoom;

            view.display_min = display_min;
            view.display_max = display_max;
            view.normalize_for_display = normalize_for_display;

            wz::gpu::dx12::submit_scalar_field_debug_frame(device, view);

            wz::gpu::end_frame(device);
            wz::gpu::present(device);
        }

        wz::input::shutdown_raw_input();
        renderable_cache.clear();
        wz::gpu::destroy_device(device);
        wz::window::destroy_window(window);
    }

    _CrtMemCheckpoint(&mem_end);

    if (_CrtMemDifference(&mem_diff, &mem_start, &mem_end))
    {
        _CrtMemDumpStatistics(&mem_diff);
        _CrtMemDumpAllObjectsSince(&mem_start);
    }

    return 0;
}