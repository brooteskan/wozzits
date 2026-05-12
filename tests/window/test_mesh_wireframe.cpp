#include <iostream>

#include <window/window2.h>
#include <event/platform_event.h>
#include <input/input.h>

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>
#include <gpu/mesh.h>
#include <gpu/dx12/dx12.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/mesh/mesh.h>
#include <engine/assets/mesh_asset_module.h>

#include <logging/logger.h>
#include <file/filesystem.h>

#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>

namespace
{
    void set_identity(float m[16])
    {
        for (int i = 0; i < 16; ++i)
            m[i] = 0.0f;

        m[0] = 1.0f;
        m[5] = 1.0f;
        m[10] = 1.0f;
        m[15] = 1.0f;
    }
}

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
        wz::logging::init_logger(logger, {});

        // ── window ────────────────────────────────────────────────────────
        wz::window::WindowDesc desc;
        desc.title = "Wozzits Mesh Wireframe Debug";
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

        // ── procedural mesh asset ─────────────────────────────────────────
        MeshAsset mesh_asset = assets.meshes().create_procedural_mesh({
            .name = "debug/procedural_triangle",
            .kind = ProceduralMeshKind::Triangle,
            });

        if (!mesh_asset.valid())
            return 1;

        // ── mesh wireframe debug shaders ──────────────────────────────────
        auto shaders = assets.shaders().create_shader_pair({
            .name = "mesh_wireframe_debug",
            .vertex_path = "shaders/mesh_wireframe/mesh_wireframe_vs.hlsl",
            .pixel_path = "shaders/mesh_wireframe/mesh_wireframe_ps.hlsl",
            });

        if (!shaders.valid())
            return 1;

        // ── resolve assets ────────────────────────────────────────────────
        if (!assets.commit())
            return 1;

        const auto report = assets.resolve_all();
        if (!report.ok())
            return 1;

        auto shader_handles = assets.shaders().get_shader_pair(shaders);
        if (!shader_handles.valid())
            return 1;

        MeshHandle mesh_handle = assets.meshes().get_mesh(mesh_asset);
        if (!mesh_handle.valid())
            return 1;

        const MeshData* mesh_data =
            assets.meshes().get_mesh_data(mesh_handle);

        if (!mesh_data || !mesh_data->valid())
            return 1;

        // ── upload mesh to GPU buffers ────────────────────────────────────
        wz::gpu::GPUHandle gpu_mesh =
            wz::gpu::upload_mesh(device, *mesh_data);

        if (!gpu_mesh.valid())
            return 1;

        if (gpu_mesh.type != wz::gpu::GPUResourceType::Mesh)
            return 1;

        // ── create mesh wireframe debug draw context ──────────────────────
        wz::gpu::dx12::create_mesh_wireframe_debug_context(device, {
            .vertex_shader = shader_handles.vertex,
            .pixel_shader = shader_handles.pixel,
            .mesh = gpu_mesh,
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

            wz::gpu::dx12::MeshWireframeDebugView view{};
            set_identity(view.world);
            set_identity(view.view_proj);

            wz::gpu::dx12::submit_mesh_wireframe_debug_frame(device, view);

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