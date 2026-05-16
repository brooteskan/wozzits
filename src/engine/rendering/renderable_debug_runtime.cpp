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
}