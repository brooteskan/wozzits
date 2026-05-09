// src/engine/app_context.cpp
#include <engine/app_context.h>
#include <logging/logger.h>

namespace wz::engine
{
    bool init(AppContext& ctx, const AppDesc& desc)
    {
        wz::logging::init_logger(ctx.logger, {});

        ctx.window = wz::window::create_window(desc.window);
        if (!ctx.window.valid())
            return false;

        ctx.device = wz::gpu::create_device(ctx.window);
        if (!ctx.device.valid())
        {
            wz::window::destroy_window(ctx.window);
            ctx.window = {};
            return false;
        }

        ctx.assets = std::make_unique<wz::engine::assets::EngineAssetLibrary>(
            ctx.device,
            ctx.logger,
            desc.resource_root
        );

        return true;
    }

    void shutdown(AppContext& ctx)
    {
        ctx.assets.reset();
        wz::logging::shutdown_logger(ctx.logger);

        if (ctx.device.valid())
            wz::gpu::destroy_device(ctx.device);

        if (ctx.window.valid())
            wz::window::destroy_window(ctx.window);

        ctx.device = {};
        ctx.window = {};
    }
}
