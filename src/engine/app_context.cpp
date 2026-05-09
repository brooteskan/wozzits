// src/engine/app_context.cpp
#include <engine/app_context.h>

namespace wz::engine
{
    bool init(AppContext& ctx, const AppDesc& desc)
    {
        ctx.logger.set_callback(wz::LogSinkType::Stderr);

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

        if (ctx.device.valid())
            wz::gpu::destroy_device(ctx.device);

        if (ctx.window.valid())
            wz::window::destroy_window(ctx.window);

        ctx.device = {};
        ctx.window = {};
    }
}
