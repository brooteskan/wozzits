// src/engine/app_context.cpp
#include <engine/app_context.h>
#include <logging/logger.h>

namespace wz::engine
{
    bool init(AppContext& ctx, const AppDesc& desc)
    {
        wz::logging::init_logger(ctx.logger, desc.logger);
        ctx.logger.info("engine initializing");

        ctx.window = wz::window::create_window(desc.window);
        if (!ctx.window.valid())
        {
            ctx.logger.error("failed to create window");
            return false;
        }
        ctx.logger.info("window created");

        ctx.device = wz::gpu::create_device(ctx.window);
        if (!ctx.device.valid())
        {
            ctx.logger.error("failed to create GPU device");
            wz::window::destroy_window(ctx.window);
            ctx.window = {};
            return false;
        }
        ctx.logger.info("GPU device created");

        ctx.assets = std::make_unique<wz::engine::assets::EngineAssetLibrary>(
            ctx.device,
            ctx.logger,
            desc.resource_root
        );

        ctx.logger.info("engine ready");
        return true;
    }

    void shutdown(AppContext& ctx)
    {
        ctx.logger.info("engine shutting down");

        ctx.assets.reset();

        if (ctx.device.valid())
            wz::gpu::destroy_device(ctx.device);

        if (ctx.window.valid())
            wz::window::destroy_window(ctx.window);

        ctx.device = {};
        ctx.window = {};

        wz::logging::shutdown_logger(ctx.logger); // last — outlives everything
    }
}
