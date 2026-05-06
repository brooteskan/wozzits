#include <iostream>

#include <window/window2.h>
#include <event/platform_event.h>
#include <input/input.h>

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>
#include <gpu/scalar_field_texture.h>
#include <gpu/dx12/dx12.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/scalar_field/scalar_field.h>

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
        // ── logger ────────────────────────────────────────────────────────
        wz::Logger logger;
        logger.set_callback(wz::LogSinkType::Stderr);

        // ── window ────────────────────────────────────────────────────────
        wz::window::WindowDesc desc;
        desc.title = "Wozzits Scalar Field Debug";
        desc.width = 800;
        desc.height = 600;

        wz::window::WindowHandle window = wz::window::create_window(desc);
        if (!window.valid())
            return 1;

        // ── device ────────────────────────────────────────────────────────
        wz::gpu::Device device = wz::gpu::create_device(window);
        if (!device.valid())
            return 1;

        wz::input::init_raw_input();

        // ── asset library ─────────────────────────────────────────────────
        wz::engine::assets::EngineAssetLibrary assets{
            device,
            logger,
            wz::fs::Path{ "resources" }
        };

        using namespace wz::engine::assets;

        // ── scalar field asset ────────────────────────────────────────────
        ScalarFieldAsset field = assets.create_procedural_scalar_field({
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
            return 1;

        // ── scalar field debug shaders ────────────────────────────────────
        auto shaders = assets.create_shader_pair({
            .name = "scalar_field_debug",
            .vertex_path = "shaders/scalar_field/scalar_field_vs.hlsl",
            .pixel_path = "shaders/scalar_field/scalar_field_ps.hlsl",
            });

        if (!shaders.valid())
            return 1;

        // ── resolve assets ────────────────────────────────────────────────
        if (!assets.commit())
            return 1;

        assets.resolve_all();

        auto shader_handles = assets.get_shader_pair(shaders);
        if (!shader_handles.valid())
            return 1;

        ScalarFieldHandle scalar_handle = assets.get_scalar_field(field);
        if (!scalar_handle.valid())
            return 1;

        const ScalarFieldData* scalar_data =
            assets.get_scalar_field_data(scalar_handle);

        if (!scalar_data || !scalar_data->valid())
            return 1;

        // ── upload scalar field to GPU texture ────────────────────────────
        wz::gpu::GPUHandle texture =
            wz::gpu::upload_scalar_field_texture(device, *scalar_data);

        if (!texture.valid())
            return 1;

        if (texture.type != wz::gpu::GPUResourceType::Texture)
            return 1;

        // ── create scalar field debug draw context ────────────────────────
        wz::gpu::dx12::create_scalar_field_debug_context(device, {
            .vertex_shader = shader_handles.vertex,
            .pixel_shader = shader_handles.pixel,
            .scalar_field_texture = texture,
            });

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
                        event.resize.height
                    );
                }

                if (event.type == PlatformEvent::Type::Close)
                {
                    std::cout << "Close event\n";
                }
            }

            wz::gpu::begin_frame(device);
            wz::gpu::clear(device, 0.05f, 0.05f, 0.05f, 1.0f);

            wz::gpu::dx12::submit_scalar_field_debug_frame(device);

            wz::gpu::end_frame(device);
            wz::gpu::present(device);
        }

        // ── shutdown ──────────────────────────────────────────────────────
        wz::input::shutdown_raw_input();
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