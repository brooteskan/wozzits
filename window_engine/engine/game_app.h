#pragma once
// engine/game_app.h

#include <memory>


#include <window/window2.h>
#include <input/input.h>

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>

#include <engine/assets/engine_asset_library.h>

#include <engine/engine.h>
#include <engine/runtime_camera.h>

#include <logging/logger.h>

#include <scene/compile/scene_compiler.h>
#include <render/frame/render_frame.h>


namespace wz::app
{
    struct DebugObjectRuntime
    {
        wz::scene::SceneStorage scene{};
        std::vector<wz::scene::RenderableDescriptor> descriptors{};

        bool ready = false;
    };

    struct ScalarFieldDebugRuntime
    {
        wz::gpu::GPUHandle texture{};
        bool ready = false;
    };

    struct GameApp
    {
        wz::window::WindowHandle window{};
        wz::gpu::Device device{};
        wz::Logger logger{};

        std::unique_ptr<wz::engine::assets::EngineAssetLibrary> assets{};

        RuntimeCamera camera{};

        ScalarFieldDebugRuntime scalar_debug{};

        DebugObjectRuntime debug_object{};

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