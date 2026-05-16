// window_engine/engine/rendering/renderable_gpu_cache.h
#pragma once

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/renderable/renderable.h>
#include <engine/assets/renderable_asset_module.h>

#include <vector>

namespace wz::engine::rendering
{
    struct PreparedRenderable
    {
        wz::engine::assets::RenderableKind kind{};
        wz::asset::AssetKey source_asset{};

        wz::gpu::GPUHandle gpu_resource{};

        wz::engine::assets::BuiltinRenderProgram program{};
        wz::engine::assets::RenderDomain domain{};
        uint32_t policy_flags = wz::engine::assets::RenderPolicy_None;

        bool valid() const noexcept
        {
            return gpu_resource.valid();
        }
    };

    // Runtime helper that realizes RenderableAssetData into backend GPU handles.
    //
    // This cache does not own the GPU resources it remembers. Backend/device
    // resource tables own the resources. clear() only forgets cached mappings.
    class RenderableGpuCache
    {
    public:
        PreparedRenderable realize(
            wz::gpu::Device& device,
            wz::engine::assets::EngineAssetLibrary& assets,
            wz::engine::assets::RenderableHandle handle);

        void clear();

    private:
        struct Entry
        {
            wz::asset::AssetKey source_asset{};
            wz::engine::assets::RenderableKind kind{};
            wz::gpu::GPUHandle gpu_resource{};
        };

        const Entry* find(
            wz::asset::AssetKey source_asset,
            wz::engine::assets::RenderableKind kind) const;

        void add(
            wz::asset::AssetKey source_asset,
            wz::engine::assets::RenderableKind kind,
            wz::gpu::GPUHandle gpu_resource);

        std::vector<Entry> entries_;
    };
}