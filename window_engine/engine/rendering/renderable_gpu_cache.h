// window_engine/engine/rendering/renderable_gpu_cache.h
#pragma once

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>
#include <gpu/scoped_gpu_handle.h>

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
        wz::gpu::GPUHandle mesh_field_visualization_resource{};

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
    // All GPU resources created by the cache are RAII-managed via
    // ScopedGPUHandle and a DeferredReleaseQueue. Clearing or destroying the
    // cache automatically queues resources for deferred release — there is no
    // separate "release" overload to forget to call.
    class RenderableGpuCache
    {
    public:
        // The cache requires a deferred release queue for GPU resource
        // lifecycle management. The queue must outlive the cache.
        explicit RenderableGpuCache(
            wz::gpu::DeferredReleaseQueue& release_queue);

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

        PreparedRenderable find_mesh_data(
            wz::asset::AssetKey cache_key,
            wz::engine::assets::BuiltinRenderProgram program,
            wz::asset::ResourceHandle render_program,
            wz::engine::assets::RenderDomain domain,
            uint32_t policy_flags) const;

        const std::vector<wz::engine::assets::TerrainVisualChunk>*
        find_terrain_mesh_chunks(wz::asset::AssetKey terrain_asset) const;

        const std::vector<TerrainTransitionDrawRange>*
        find_terrain_transition_ranges(
            wz::asset::AssetKey terrain_asset) const;

        void add_terrain_mesh_chunks(
            wz::asset::AssetKey terrain_asset,
            std::vector<wz::engine::assets::TerrainVisualChunk> chunks);

        void add_terrain_transition_ranges(
            wz::asset::AssetKey terrain_asset,
            std::vector<TerrainTransitionDrawRange> ranges);

        const std::vector<TerrainFarSplatChunk>* find_terrain_far_splat_chunks(
            wz::asset::AssetKey terrain_asset) const;

        void add_terrain_far_splat_chunks(
            wz::asset::AssetKey terrain_asset,
            std::vector<TerrainFarSplatChunk> chunks,
            std::vector<wz::gpu::GPUHandle> gpu_resources = {});

        // Forget all cached entries. RAII handles automatically queue their
        // GPU resources for deferred release — no manual release needed.
        void clear();

        void swap_with(RenderableGpuCache& other) noexcept;

    private:
        struct Entry
        {
            wz::asset::AssetKey source_asset{};
            wz::engine::assets::RenderableKind kind{};
            wz::gpu::ScopedGPUHandle gpu_resource{};
        };

        const Entry* find(
            wz::asset::AssetKey source_asset,
            wz::engine::assets::RenderableKind kind) const;

        void add(
            wz::asset::AssetKey source_asset,
            wz::engine::assets::RenderableKind kind,
            wz::gpu::ScopedGPUHandle gpu_resource);

        wz::gpu::DeferredReleaseQueue& release_queue_;
        std::vector<Entry> entries_;

        struct TerrainMeshChunkEntry
        {
            wz::asset::AssetKey terrain_asset{};
            std::vector<wz::engine::assets::TerrainVisualChunk> chunks;
            std::vector<TerrainTransitionDrawRange> transition_ranges;
        };
        std::vector<TerrainMeshChunkEntry> terrain_mesh_chunk_entries_;

        struct TerrainFarSplatEntry
        {
            wz::asset::AssetKey terrain_asset{};
            std::vector<wz::gpu::ScopedGPUHandle> gpu_resources;
            std::vector<TerrainFarSplatChunk> chunks;
        };
        std::vector<TerrainFarSplatEntry> terrain_far_splat_entries_;
    };
}
