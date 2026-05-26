// src/engine/rendering/scene_render_resource_resolver.cpp

#include <engine/rendering/scene_render_resource_resolver.h>

namespace wz::engine::rendering
{
    MeshSceneRenderResourceResolver::MeshSceneRenderResourceResolver(
        wz::engine::assets::MeshAssetModule& meshes,
        RenderResourceResolver& render_resolver)
        : meshes_(meshes)
        , render_resolver_(render_resolver)
    {
    }

    bool MeshSceneRenderResourceResolver::realize_renderable_descriptor(
        const wz::engine::assets::RenderableAssetData& renderable,
        wz::scene::RenderableDescriptor& descriptor) const
    {
        if (renderable.kind != wz::engine::assets::RenderableKind::Mesh)
            return false;

        const wz::engine::assets::MeshAsset mesh_asset{
            .output = renderable.source_asset,
        };

        const wz::engine::assets::MeshHandle mesh_handle =
            meshes_.get_mesh(mesh_asset);

        if (!mesh_handle.valid())
            return false;

        // Register the mesh with the render resource resolver.
        // In the full runtime, gpu_resource would come from gpu::upload_mesh().
        // Here we use the CPU-side ResourceHandle as the GPUHandle for
        // registration — they are the same type (ResourceHandle).  The render
        // resource resolver stores it and returns a scene-render MeshHandle.
        const wz::scene::MeshHandle scene_mesh =
            render_resolver_.register_mesh(
                mesh_handle.handle,
                renderable.program);

        descriptor.mesh = scene_mesh;

        // Default material: scene-render INVALID_MATERIAL signals the submit
        // path to use the debug/fallback material.  A material resolver would
        // be added when material assets are introduced.
        descriptor.material = wz::scene::INVALID_MATERIAL;

        return true;
    }
}
