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

#include <gpu/gpu.h>

namespace wz::engine::rendering
{
    [[nodiscard]] bool create_debug_context_for_prepared_renderable(
        wz::gpu::Device& device,
        const PreparedRenderable& prepared,
        const wz::engine::assets::ShaderPairHandles& shaders);
}