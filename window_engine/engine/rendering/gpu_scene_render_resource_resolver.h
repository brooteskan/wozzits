// window_engine/engine/rendering/gpu_scene_render_resource_resolver.h
//
// GPU-aware SceneRenderResourceResolver for runtime/toolhost use.
// Realizes mesh renderables by uploading via gpu::upload_mesh and
// registering the result with RenderResourceResolver to produce real
// scene-render handles.
#pragma once

#include <engine/assets/scene/scene_instance.h>
#include <engine/rendering/render_resource_resolver.h>
#include <engine/rendering/renderable_gpu_cache.h>
#include <engine/assets/engine_asset_library.h>

#include <gpu/gpu.h>
#include <gpu/scoped_gpu_handle.h>

#include <vector>

namespace wz::engine::rendering
{
    class GpuSceneRenderResourceResolver final
        : public wz::engine::assets::SceneRenderResourceResolver
    {
    public:
        GpuSceneRenderResourceResolver(
            wz::gpu::Device& device,
            wz::engine::assets::EngineAssetLibrary& assets,
            RenderResourceResolver& render_resolver,
            RenderableGpuCache* cache = nullptr);
        ~GpuSceneRenderResourceResolver() override;

        bool realize_renderable_descriptor(
            const wz::engine::assets::RenderableAssetData& renderable,
            wz::scene::RenderableDescriptor& descriptor) const override;

    private:
        wz::gpu::Device& device_;
        wz::engine::assets::EngineAssetLibrary& assets_;
        RenderResourceResolver& render_resolver_;
        RenderableGpuCache* cache_ = nullptr;
        mutable wz::gpu::DeferredReleaseQueue preview_release_queue_{};
        mutable std::vector<wz::gpu::ScopedGPUHandle> preview_gpu_resources_{};
    };
}
