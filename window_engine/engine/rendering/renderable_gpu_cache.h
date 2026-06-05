// window_engine/engine/rendering/renderable_gpu_cache.h
#pragma once

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/renderable/renderable.h>
#include <engine/assets/renderable_asset_module.h>
#include <engine/rendering/render_resource_resolver.h>

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

        // Mirrors RenderableAssetData::render_program.  Invalid unless
        // explicitly set via RenderableAssetData before realize() is called.
        wz::asset::ResourceHandle render_program{};

        bool valid() const noexcept
        {
            return gpu_resource.valid();
        }
    };

    // Runtime helper that realizes RenderableAssetData into backend GPU handles.
    //
    // Backend/device resource tables own realized GPU resources. clear() only
    // forgets cached mappings; clear(device) also releases cache-owned mesh
    // handles from the backend table.
    class RenderableGpuCache
    {
    public:
        PreparedRenderable realize(
            wz::gpu::Device& device,
            wz::engine::assets::EngineAssetLibrary& assets,
            wz::engine::assets::RenderableHandle handle);

        PreparedRenderable realize_data(
            wz::gpu::Device& device,
            wz::engine::assets::EngineAssetLibrary& assets,
            const wz::engine::assets::RenderableAssetData& renderable);

        PreparedRenderable realize_mesh_data(
            wz::gpu::Device& device,
            wz::asset::AssetKey cache_key,
            const wz::engine::assets::MeshData& mesh,
            wz::engine::assets::BuiltinRenderProgram program,
            wz::asset::ResourceHandle render_program,
            wz::engine::assets::RenderDomain domain,
            uint32_t policy_flags);

        const std::vector<TerrainFarSplatChunk>* find_terrain_far_splat_chunks(
            wz::asset::AssetKey terrain_asset) const;

        void add_terrain_far_splat_chunks(
            wz::asset::AssetKey terrain_asset,
            std::vector<TerrainFarSplatChunk> chunks);

        void clear();
        void clear(wz::gpu::Device& device);

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
        std::vector<std::pair<
            wz::asset::AssetKey,
            std::vector<TerrainFarSplatChunk>>> terrain_far_splat_entries_;
    };
}
