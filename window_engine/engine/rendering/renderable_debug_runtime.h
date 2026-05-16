#pragma once

// engine/rendering/renderable_debug_runtime.h
//
// Runtime helper for connecting PreparedRenderable objects to the existing
// DX12 diagnostic render contexts.
//
// This is not the final render path and not a generic pipeline cache. It is
// a small bridge used by debug windows while the renderable asset/runtime
// model is being developed.

#include <engine/rendering/renderable_gpu_cache.h>

#include <engine/assets/shader_asset_module.h>
#include <engine/assets/engine_asset_library.h>
#include <engine/assets/renderable_asset_module.h>
#include <engine/rendering/renderable_gpu_cache.h>
#include <gpu/gpu.h>

namespace wz::engine::rendering
{
    [[nodiscard]] bool create_debug_context_for_prepared_renderable(
        wz::gpu::Device& device,
        const PreparedRenderable& prepared,
        const wz::engine::assets::ShaderPairHandles& shaders);


    struct DebugRenderableSetup
    {
        PreparedRenderable prepared{};

        bool valid() const noexcept
        {
            return prepared.valid();
        }
    };

    [[nodiscard]] bool setup_debug_renderable_context(
        wz::gpu::Device& device,
        wz::engine::assets::EngineAssetLibrary& assets,
        RenderableGpuCache& cache,
        const wz::engine::assets::RenderableAsset& renderable_asset,
        const wz::engine::assets::ShaderPairAsset& shader_pair_asset,
        DebugRenderableSetup& out);
}