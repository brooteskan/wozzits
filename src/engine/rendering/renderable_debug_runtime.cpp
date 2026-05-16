// src/engine/rendering/renderable_debug_runtime.cpp

#include <engine/rendering/renderable_debug_runtime.h>

#include <gpu/dx12/dx12.h>

namespace wz::engine::rendering
{
    bool create_debug_context_for_prepared_renderable(
        wz::gpu::Device& device,
        const PreparedRenderable& prepared,
        const wz::engine::assets::ShaderPairHandles& shaders)
    {
        if (!device.valid())
            return false;

        if (!prepared.valid())
            return false;

        if (!shaders.valid())
            return false;

        using namespace wz::engine::assets;

        switch (prepared.program)
        {
        case BuiltinRenderProgram::MeshWireframeDebug:
        {
            if (prepared.kind != RenderableKind::Mesh)
                return false;

            if (prepared.gpu_resource.type != wz::gpu::GPUResourceType::Mesh)
                return false;

            wz::gpu::dx12::create_mesh_wireframe_debug_context(
                device,
                wz::gpu::dx12::MeshWireframeDebugContextDesc{
                    .vertex_shader = shaders.vertex,
                    .pixel_shader = shaders.pixel,
                    .mesh = prepared.gpu_resource,
                });

            return true;
        }

        case BuiltinRenderProgram::GaussianSplatDebug:
        {
            if (prepared.kind != RenderableKind::GaussianSplatCloud)
                return false;

            if (prepared.gpu_resource.type !=
                wz::gpu::GPUResourceType::GaussianSplatCloud)
            {
                return false;
            }

            wz::gpu::dx12::create_gaussian_splat_debug_context(
                device,
                wz::gpu::dx12::GaussianSplatDebugContextDesc{
                    .vertex_shader = shaders.vertex,
                    .pixel_shader = shaders.pixel,
                    .splat_cloud = prepared.gpu_resource,
                });

            return true;
        }
        }

        return false;
    }

    bool setup_debug_renderable_context(
        wz::gpu::Device& device,
        wz::engine::assets::EngineAssetLibrary& assets,
        RenderableGpuCache& cache,
        const wz::engine::assets::RenderableAsset& renderable_asset,
        const wz::engine::assets::ShaderPairAsset& shader_pair_asset,
        DebugRenderableSetup& out)
    {
        out = {};

        if (!device.valid())
            return false;

        if (!renderable_asset.valid())
            return false;

        if (!shader_pair_asset.valid())
            return false;

        const wz::engine::assets::ShaderPairHandles shader_handles =
            assets.shaders().get_shader_pair(shader_pair_asset);

        if (!shader_handles.valid())
            return false;

        const wz::engine::assets::RenderableHandle renderable_handle =
            assets.renderables().get_renderable(renderable_asset);

        if (!renderable_handle.valid())
            return false;

        const PreparedRenderable prepared =
            cache.realize(
                device,
                assets,
                renderable_handle);

        if (!prepared.valid())
            return false;

        if (!create_debug_context_for_prepared_renderable(
            device,
            prepared,
            shader_handles))
        {
            return false;
        }

        out.prepared = prepared;
        return true;
    }
}