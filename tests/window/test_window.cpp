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
#include <engine/assets/schema_registry.h>
#include <engine/assets/shader/shader_types.h>
#include <asset/compiler.h>
#include <gpu/shader.h>

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

static Path triangle_shader_assets_path()
{
    return Path{ "resources\\shaders\\triangle" };
}


// ─── main ─────────────────────────────────────────────────────────────────────


int main()
{
    // ── setup window / device / inputs ─────────────────────────────────────────────────────────────
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    // _CrtSetBreakAlloc(380);

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


    // ── register compilers ─────────────────────────────────────────────────────────────
    //
    // File carrier compiler — preserves the byte payload so the shader
    // compiler's dep_nodes contain live bytes, not an overwritten handle.
    // Returns Compiled stage with an empty ResourceHandle (no GPU resource).

    // ── build triangle test assets ────────────────────────────────────────────────

    namespace test_assets = wz::engine::assets::test;

    wz::asset::CompilerRegistry registry =
        test_assets::make_triangle_test_compiler_registry(device, logger);

    wz::asset::AssetSystem asset_sys(std::move(registry));

    test_assets::TriangleTestResources triangle_resources =
        test_assets::initialize_triangle_test_assets(
            asset_sys,
            triangle_shader_assets_path(),
            logger
        );

    wz::gpu::dx12::TriangleTestContextDesc triangle_desc{
    .vertex_shader = triangle_resources.vertex_shader,
    .pixel_shader = triangle_resources.pixel_shader,
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

        // wz::gpu::dx12::draw_test_triangle(device);
        wz::gpu::dx12::submit_triangle_test_frame(device);

        wz::gpu::end_frame(device);
        wz::gpu::present(device);
    }

    // ── shutdown ───────────────────────────────────────────────────────────────
    wz::input::shutdown_raw_input();
    wz::gpu::destroy_device(device);
    destroy_window(window);


    return 0;
}