// src/engine/rendering/renderable_pipeline_cache.cpp

#include <engine/rendering/renderable_pipeline_cache.h>
#include <gpu/dx12/dx12_internal.h>

namespace wz::engine::rendering
{
    bool RenderablePipelineCache::realize(
        wz::gpu::Device&                         device,
        wz::engine::assets::BuiltinRenderProgram program,
        wz::gpu::GPUHandle                       vertex_shader,
        wz::gpu::GPUHandle                       pixel_shader)
    {
        if (!valid_program(program))
            return false;

        if (!vertex_shader.valid() || !pixel_shader.valid())
            return false;

        const size_t i = index_of(program);

        if (entries_[i].valid())
            return false;  // already realized — call clear() first to replace

        const wz::gpu::GPUHandle handle =
            wz::gpu::dx12::internal::create_graphics_pipeline(
                device, program, vertex_shader, pixel_shader);

        if (!handle.valid())
            return false;

        entries_[i] = handle;
        return true;
    }

    bool RenderablePipelineCache::realize(
        wz::gpu::Device&                             device,
        wz::engine::assets::EngineAssetLibrary&      assets,
        wz::engine::assets::BuiltinRenderProgram     program,
        const wz::engine::assets::ShaderPairAsset&   shaders)
    {
        const wz::engine::assets::ShaderPairHandles handles =
            assets.shaders().get_shader_pair(shaders);

        if (!handles.valid())
            return false;

        return realize(device, program, handles.vertex, handles.pixel);
    }

    wz::gpu::GPUHandle RenderablePipelineCache::get(
        wz::engine::assets::BuiltinRenderProgram program) const noexcept
    {
        if (!valid_program(program))
            return wz::gpu::GPUHandle{};
        return entries_[index_of(program)];
    }

    void RenderablePipelineCache::clear() noexcept
    {
        entries_.fill(wz::gpu::GPUHandle{});
    }
}
