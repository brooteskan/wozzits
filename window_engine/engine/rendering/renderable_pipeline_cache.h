#pragma once
// window_engine/engine/rendering/renderable_pipeline_cache.h
//
// Realizes and owns graphics pipelines (root signature + PSO) keyed by
// BuiltinRenderProgram.
//
// Usage:
//   1. Call realize() once per program after assets are resolved.
//   2. Pass the cache to submit_render_frame(device, frame, resolver, cache).
//
// This replaces the device-singleton debug context pattern
// (mesh_wire_debug_ctx, gaussian_splat_debug_ctx, …) for the production
// submit path.  Debug contexts remain available as compatibility wrappers
// during the transition.

#include <gpu/gpu.h>
#include <engine/assets/renderable/renderable.h>
#include <engine/assets/shader_asset_module.h>
#include <engine/assets/engine_asset_library.h>

#include <array>

namespace wz::engine::rendering
{
    class RenderablePipelineCache
    {
    public:
        // Compile and store the pipeline for a program using already-resolved
        // shader handles.  Returns false if the program is already registered,
        // the handles are invalid, or PSO creation fails.
        bool realize(
            wz::gpu::Device&                         device,
            wz::engine::assets::BuiltinRenderProgram program,
            wz::gpu::GPUHandle                       vertex_shader,
            wz::gpu::GPUHandle                       pixel_shader);

        // Convenience overload: resolves shader handles from the asset library.
        bool realize(
            wz::gpu::Device&                              device,
            wz::engine::assets::EngineAssetLibrary&       assets,
            wz::engine::assets::BuiltinRenderProgram      program,
            const wz::engine::assets::ShaderPairAsset&    shaders);

        // Returns the GPUHandle for the pipeline registered under this program,
        // or an invalid handle if realize() was never called for it.
        wz::gpu::GPUHandle get(
            wz::engine::assets::BuiltinRenderProgram program) const noexcept;

        // Forget all registered handles (does not release GPU resources — those
        // are owned by the device's DX12GraphicsPipelineTable).
        void clear() noexcept;

    private:
        static size_t index_of(wz::engine::assets::BuiltinRenderProgram program) noexcept
        {
            return static_cast<size_t>(program);
        }

        std::array<wz::gpu::GPUHandle,
                   wz::engine::assets::kBuiltinRenderProgramCount> entries_{};
    };
}
