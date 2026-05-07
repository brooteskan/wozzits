#pragma once
// engine/game_app.h

#include <memory>


#include <window/window2.h>
#include <input/input.h>

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>

#include <engine/assets/engine_asset_library.h>

#include <engine/engine.h>

#include <logging/logger.h>


namespace wz::app
{
    struct RuntimeCamera
    {
        float x = 0.0f;
        float y = 0.0f;

        float yaw = 0.0f;
        float pitch = 0.0f;

        float move_speed = 2.0f;
        float look_speed = 0.0025f;
    };

    struct GameApp
    {
        wz::window::WindowHandle window{};
        wz::gpu::Device device{};
        wz::Logger logger{};

        std::unique_ptr<wz::engine::assets::EngineAssetLibrary> assets{};

        RuntimeCamera camera{};

        bool initialized = false;
    };

    bool init(GameApp& app);

    void update(
        wz::engine::Context& ctx,
        wz::engine::FrameContext& fctx,
        GameApp& app);

    void render(
        GameApp& app,
        const wz::engine::FrameContext& fctx);

    void shutdown(GameApp& app);
}