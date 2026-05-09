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

namespace wz::engine
{
    struct AppContext
    {
        wz::window::WindowHandle                                 window{};
        wz::gpu::Device                                          device{};
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
        wz::window::WindowDesc window{};
        wz::fs::Path           resource_root{ "resources" };
    };

    bool init(AppContext& ctx, const AppDesc& desc);
    void shutdown(AppContext& ctx);
}
