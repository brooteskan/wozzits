// src/engine/rendering/gpu_scene_render_resource_resolver.cpp

#include <engine/rendering/gpu_scene_render_resource_resolver.h>

#include <engine/assets/mesh_asset_module.h>
#include <gpu/mesh.h>

namespace wz::engine::rendering
{
    GpuSceneRenderResourceResolver::GpuSceneRenderResourceResolver(
        wz::gpu::Device& device,
        wz::engine::assets::EngineAssetLibrary& assets,
        RenderResourceResolver& render_resolver)
        : device_(device)
        , assets_(assets)
        , render_resolver_(render_resolver)
    {
    }

    bool GpuSceneRenderResourceResolver::realize_renderable_descriptor(
        const wz::engine::assets::RenderableAssetData& renderable,
        wz::scene::RenderableDescriptor& descriptor) const
    {
        if (renderable.kind != wz::engine::assets::RenderableKind::Mesh)
            return false;

        const wz::engine::assets::MeshAsset mesh_asset{
            .output = renderable.source_asset,
        };

        const wz::engine::assets::MeshHandle mesh_handle =
            assets_.meshes().get_mesh(mesh_asset);

        if (!mesh_handle.valid())
            return false;

        const wz::engine::assets::MeshData* mesh_data =
            assets_.meshes().get_mesh_data(mesh_handle);

        if (!mesh_data || !mesh_data->valid())
            return false;

        const wz::gpu::GPUHandle gpu_mesh =
            wz::gpu::upload_mesh(device_, *mesh_data);

        if (!gpu_mesh.valid())
            return false;

        const wz::scene::MeshHandle scene_mesh =
            render_resolver_.register_mesh(
                gpu_mesh,
                renderable.program,
                renderable.render_program);

        descriptor.mesh = scene_mesh;
        descriptor.material = wz::scene::INVALID_MATERIAL;

        return true;
    }
}
