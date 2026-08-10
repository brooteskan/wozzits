#pragma once
// engine/app_context.h

#include <memory>
#include <string>

#include <file/filesystem.h>
#include <logging/logger.h>
#include <window/window_types.h>
#include <window/window2.h>
#include <gpu/gpu.h>
#include <engine/assets/engine_asset_library.h>
#include <engine/rendering/engine_gpu_context.h>

namespace wz::engine
{
    struct AppContext
    {
        wz::window::WindowHandle                                 window{};
        wz::gpu::Device                                          device{};
        std::unique_ptr<wz::engine::rendering::EngineGpuContext> gpu{};
        wz::Logger                                               logger{};
        std::unique_ptr<wz::engine::assets::EngineAssetLibrary> assets{};

        AppContext() = default;
        AppContext(const AppContext&)            = delete;
        AppContext& operator=(const AppContext&) = delete;
        AppContext(AppContext&&)                 = delete;
        AppContext& operator=(AppContext&&)      = delete;
    };

    struct AppDesc
    {
        wz::window::WindowDesc   window{};
        wz::fs::Path             resource_root{ "resources" };
        wz::engine::assets::EngineAssetCacheSettings asset_cache{};
        wz::logging::LoggerDesc  logger{ .enable_debugger_sink = true };
    };

    bool init(AppContext& ctx, const AppDesc& desc);

    // Full teardown: shutdown_subsystems() then shutdown_logging(). What a caller
    // with no background threads of its own wants.
    void shutdown(AppContext& ctx);

    // ── two-phase teardown, for callers that own threads ────────────────────
    //
    // A caller with background lanes cannot use the one-call form. The logger's
    // state is a plain heap pointer, so freeing it while ANY thread might still
    // log is a use-after-free -- and lane threads have to be joined AFTER
    // destroy_device (the AMD-driver ordering that governs every lane in
    // run_project_runtime), which means they are necessarily still alive when
    // the one-call form would already have freed the logger.
    //
    // Split so the logger can outlive the join:
    //
    //     wz::engine::shutdown_subsystems(ctx);  // GPU, assets, device, window
    //     join_your_lane_threads();              // may still log -- safely
    //     wz::engine::shutdown_logging(ctx);     // now nothing can log
    //
    // Everything but the logger: waits for the GPU, releases the asset library
    // and GPU context, then destroys the device and the window. The logger is
    // left UP, so this is safe to log around.
    void shutdown_subsystems(AppContext& ctx);

    // Release the logger. Call once every thread that might log is joined.
    // Null-safe and idempotent, so a caller that already ran shutdown() -- or
    // that failed init before the logger came up -- can call it harmlessly.
    void shutdown_logging(AppContext& ctx);
}
