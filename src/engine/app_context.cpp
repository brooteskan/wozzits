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

        ctx.gpu =
            std::make_unique<wz::engine::rendering::EngineGpuContext>(
                ctx.device);

        ctx.assets = std::make_unique<wz::engine::assets::EngineAssetLibrary>(
            *ctx.gpu,
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

        // Wait for the GPU BEFORE freeing anything it might still be reading.
        // Since #306 end_frame signals its fence and returns without blocking, so
        // the frame loop can exit with up to kFramesInFlight frames still
        // executing. Both resets below free GPU resources immediately and with no
        // fence gate -- ctx.gpu.reset() runs ~EngineGpuContext ->
        // GpuResourceRegistry::destroy_all(), which calls backend destroy() on
        // every resident resource -- and D3D12 does NOT keep command lists'
        // resource references alive, so releasing a resource an in-flight list
        // still reads is a use-after-free on the GPU: a corrupt final frame or a
        // device-removed TDR, both sporadic by nature.
        //
        // Before #306 the blocking end_frame made "the loop exited" imply "the GPU
        // is idle", and this teardown silently relied on that. The wait that used
        // to be implicit is now explicit. destroy_device() further down does its
        // own wait_for_gpu, but that is far too late -- by then the resources the
        // in-flight frame referenced have already been released.
        if (ctx.device.valid())
            wz::gpu::wait_idle(ctx.device);

        ctx.assets.reset();
        ctx.gpu.reset();

        if (ctx.device.valid())
            wz::gpu::destroy_device(ctx.device);

        if (ctx.window.valid())
            wz::window::destroy_window(ctx.window);

        ctx.device = {};
        ctx.window = {};

        wz::logging::shutdown_logger(ctx.logger); // last — outlives everything
    }
}
