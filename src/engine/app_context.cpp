// src/engine/app_context.cpp
#include <engine/app_context.h>
#include <logging/logger.h>

#include <gpu/dx12/dx12_internal.h>
#include <window/window2.h>

#include <sstream>

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
        {
            std::ostringstream msg;
            msg << "window created handle="
                << wz::window::get_native_handle(ctx.window);
            ctx.logger.info(msg.str());
        }

        ctx.device = wz::gpu::create_device(ctx.window);
        if (!ctx.device.valid())
        {
            ctx.logger.error("failed to create GPU device");
            wz::window::destroy_window(ctx.window);
            ctx.window = {};
            return false;
        }
        {
            std::ostringstream msg;
            msg
                << "GPU device created"
                << " device_impl=" << ctx.device.impl
                << " native_device="
                << wz::gpu::dx12::internal::get_device(ctx.device);
            ctx.logger.info(msg.str());
        }

        ctx.assets = std::make_unique<wz::engine::assets::EngineAssetLibrary>(
            ctx.device,
            ctx.logger,
            desc.resource_root,
            desc.asset_cache
        );

        {
            std::ostringstream msg;
            msg
                << "engine ready"
                << " ctx=" << static_cast<const void*>(&ctx)
                << " device_impl=" << ctx.device.impl
                << " window="
                << wz::window::get_native_handle(ctx.window);
            ctx.logger.info(msg.str());
        }
        return true;
    }

    void shutdown(AppContext& ctx)
    {
        {
            std::ostringstream msg;
            msg
                << "engine shutting down"
                << " ctx=" << static_cast<const void*>(&ctx)
                << " device_impl=" << ctx.device.impl
                << " window="
                << (ctx.window.valid()
                    ? wz::window::get_native_handle(ctx.window)
                    : nullptr);
            ctx.logger.info(msg.str());
        }

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
