#include <iostream>
#include <cstdio>
#include <expected>
#include <utility>
#include <window/window2.h>
#include <logging/logger.h>
#include <file/filesystem.h>
#include <event/platform_event.h>
#include <gpu/gpu.h>
#include <input/input.h>
#include <gpu/dx12/dx12.h>
#include <asset/types.h>
#include <asset/system.h>
#include <engine/assets/shader/shader_types.h>
#include <asset/compiler.h>
#include <gpu/shader.h>
#include <engine/assets/engine_asset_library.h>
#include <engine/assets/test/triangle_shader_assets.h>

#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>

// ─── Schemas ──────────────────────────────────────────────────────────────────
// These would normally be hashes of the schema name strings, produced once at
// startup. Hard-coded here for the test main.

//static constexpr wz::asset::SchemaID kHLSLFileSchema{ 0x1a2b3c4d };  // carrier
//static constexpr wz::asset::SchemaID kHLSLSchema{ 0x5e6f7a8b };  // linker
// NOTE: defined in engine/assets/schema_registry.h

using namespace wz::asset;
using namespace wz::fs;

// ─── Compile descriptor ───────────────────────────────────────────────────────
// Carried on the shader node's meta field. Tells the compiler which entry
// point, target profile, and source ordering to use.





// ─── Helpers ──────────────────────────────────────────────────────────────────

const Path resource_root{ "resources" };


// ─── main ─────────────────────────────────────────────────────────────────────


int main()
{
#define _CRTDBG_MAP_ALLOC
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF);

    _CrtMemState mem_start;
    _CrtMemState mem_end;
    _CrtMemState mem_diff;

    _CrtMemCheckpoint(&mem_start);

    {


        wz::Logger logger;
        logger.set_callback(wz::LogSinkType::Stderr);



        wz::window::WindowDesc desc;
        desc.title = "Wozzits Window Test";
        desc.width = 800;
        desc.height = 600;

        wz::window::WindowHandle window = create_window(desc);

        wz::gpu::Device device =
            wz::gpu::create_device(window);

        wz::input::init_raw_input();


        // ── build triangle test assets ────────────────────────────────────────────────

        namespace test_assets = wz::engine::assets::test;

        wz::engine::assets::EngineAssetLibrary assets{
            device,
            logger,
            wz::fs::Path{ "resources" }
        };

        auto triangle = assets.create_shader_pair({
            .name = "triangle",
            .vertex_path = "shaders/triangle/triangle_vs.hlsl",
            .pixel_path = "shaders/triangle/triangle_ps.hlsl",
            });

        if (!triangle.valid())
            return 1;

        if (!assets.commit())
            return 1;

        assets.resolve_all();

        auto shader_handles = assets.get_shader_pair(triangle);
        if (!shader_handles.valid())
            return 1;

        wz::gpu::dx12::TriangleTestContextDesc triangle_desc{
            .vertex_shader = shader_handles.vertex,
            .pixel_shader = shader_handles.pixel,
        };

        wz::gpu::dx12::create_triangle_test_context(device, triangle_desc);

        // ── run frame loop ───────────────────────────────────────────────────────────────
        while (!window_should_close(window))
        {
            wz::window::pump_messages();

            PlatformEvent event{};
            while (poll_event(window, event))
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
            wz::gpu::clear(device, 0.1f, 0.2f, 0.6f, 1.0f);

            wz::gpu::dx12::submit_triangle_test_frame(device);

            wz::gpu::end_frame(device);
            wz::gpu::present(device);
        }

        // ── shutdown ───────────────────────────────────────────────────────────────
        wz::input::shutdown_raw_input();
        wz::gpu::destroy_device(device);
        destroy_window(window);
    }
    _CrtMemCheckpoint(&mem_end);

    if (_CrtMemDifference(&mem_diff, &mem_start, &mem_end))
    {
        _CrtMemDumpStatistics(&mem_diff);
        _CrtMemDumpAllObjectsSince(&mem_start);
    }

    return 0;
}