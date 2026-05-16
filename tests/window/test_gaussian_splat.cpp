#include <iostream>

#include <window/window2.h>
#include <event/platform_event.h>
#include <input/input.h>

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>
#include <gpu/dx12/dx12.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/gaussian_splat/gaussian_splat.h>
#include <engine/assets/gaussian_splat_asset_module.h>
#include <engine/assets/renderable_asset_module.h>

#include <engine/rendering/renderable_gpu_cache.h>
#include <engine/rendering/renderable_debug_runtime.h>
#include <engine/rendering/builtin_render_programs.h>

#include <logging/logger.h>
#include <file/filesystem.h>

#include <math/mat4.h>
#include <math/projection.h>

#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>

namespace
{
    void copy_mat4(float out[16], const wz::math::Mat4& m)
    {
        for (int i = 0; i < 16; ++i)
            out[i] = m.m[i];
    }

    wz::math::Mat4 make_world_translate(float x, float y, float z)
    {
        wz::math::Mat4 world = wz::math::mat4_identity();

        world.m[12] = x;
        world.m[13] = y;
        world.m[14] = z;

        return world;
    }

    wz::math::Mat4 make_debug_view_proj(int width, int height)
    {
        constexpr float Pi = 3.14159265358979323846f;

        const float fov = 70.0f * Pi / 180.0f;

        const float aspect =
            (width > 0 && height > 0)
            ? static_cast<float>(width) / static_cast<float>(height)
            : 800.0f / 600.0f;

        wz::math::Mat4 view = wz::math::mat4_identity();

        // Keep this consistent with the existing debug camera path:
        // objects are placed at positive z, and the view matrix is identity.
        view.m[12] = 0.0f;
        view.m[13] = 0.0f;
        view.m[14] = 0.0f;

        const wz::math::Mat4 projection =
            wz::math::projection_perspective_dx(
                fov,
                aspect,
                0.1f,
                100.0f
            );

        return wz::math::mul(projection, view);
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
        desc.title = "Wozzits Gaussian Splat Debug";
        desc.width = 800;
        desc.height = 600;

        int current_width = desc.width;
        int current_height = desc.height;

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

        // ── procedural gaussian splat asset ───────────────────────────────
        GaussianSplatCloudAsset splat_asset =
            assets.gaussian_splats().create_procedural_cloud({
                .name = "debug/procedural_splat_sphere",
                .count = 4096,
                .radius = 1.5f,
                .splat_scale = 1.0f,
                });

        if (!splat_asset.valid())
            return 1;

        // ── renderable asset from gaussian splat asset ────────────────────
        RenderableAsset renderable_asset =
            assets.renderables().create_gaussian_splat_debug({
                .name = "debug/procedural_splat_sphere_debug_renderable",
                .splat_cloud = splat_asset,
                });

        if (!renderable_asset.valid())
            return 1;

        // ── gaussian splat debug shaders ──────────────────────────────────
        ShaderPairDesc shader_desc{};

        if (!wz::engine::rendering::get_builtin_shader_pair_desc(
            BuiltinRenderProgram::GaussianSplatDebug,
            shader_desc))
        {
            return 1;
        }

        auto shaders = assets.shaders().create_shader_pair(shader_desc);

        if (!shaders.valid())
            return 1;

        // ── resolve assets ────────────────────────────────────────────────
        if (!assets.commit())
            return 1;

        const auto report = assets.resolve_all();
        if (!report.ok())
            return 1;

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

        if (prepared.kind != RenderableKind::GaussianSplatCloud)
            return 1;

        if (prepared.gpu_resource.type != wz::gpu::GPUResourceType::GaussianSplatCloud)
            return 1;

        if (prepared.program != BuiltinRenderProgram::GaussianSplatDebug)
            return 1;

        // ── frame loop ────────────────────────────────────────────────────
        while (!wz::window::window_should_close(window))
        {
            wz::window::pump_messages();

            PlatformEvent event{};
            while (wz::window::poll_event(window, event))
            {
                if (event.type == PlatformEvent::Type::Resize)
                {
                    current_width = event.resize.width;
                    current_height = event.resize.height;

                    wz::gpu::resize(
                        device,
                        current_width,
                        current_height
                    );
                }

                if (event.type == PlatformEvent::Type::Close)
                {
                    std::cout << "Close event\n";
                }
            }

            wz::gpu::begin_frame(device);
            wz::gpu::clear(device, 0.05f, 0.05f, 0.05f, 1.0f);

            wz::gpu::dx12::GaussianSplatDebugView view{};

            const wz::math::Mat4 world =
                make_world_translate(0.0f, 0.0f, 5.0f);

            const wz::math::Mat4 view_proj =
                make_debug_view_proj(current_width, current_height);

            copy_mat4(view.world, world);
            copy_mat4(view.view_proj, view_proj);

            view.viewport_and_size[0] = static_cast<float>(current_width);
            view.viewport_and_size[1] = static_cast<float>(current_height);
            view.viewport_and_size[2] = 6.0f;
            view.viewport_and_size[3] = 0.0f;

            wz::gpu::dx12::submit_gaussian_splat_debug_frame(device, view);

            wz::gpu::end_frame(device);
            wz::gpu::present(device);
        }

        // ── shutdown ──────────────────────────────────────────────────────
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