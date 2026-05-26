// window_engine/engine/rendering/scene_render_resource_resolver.h
//
// Concrete SceneRenderResourceResolver for mesh renderables.
// Bridges the asset system (RenderableAssetData.source_asset) to
// scene-render handles via RenderResourceResolver::register_mesh().
#pragma once

#include <engine/assets/scene/scene_instance.h>
#include <engine/rendering/render_resource_resolver.h>
#include <engine/assets/mesh_asset_module.h>

namespace wz::engine::rendering
{
    class MeshSceneRenderResourceResolver final
        : public wz::engine::assets::SceneRenderResourceResolver
    {
    public:
        MeshSceneRenderResourceResolver(
            wz::engine::assets::MeshAssetModule& meshes,
            RenderResourceResolver& render_resolver);

        bool realize_renderable_descriptor(
            const wz::engine::assets::RenderableAssetData& renderable,
            wz::scene::RenderableDescriptor& descriptor) const override;

    private:
        wz::engine::assets::MeshAssetModule& meshes_;
        RenderResourceResolver& render_resolver_;
    };
}
