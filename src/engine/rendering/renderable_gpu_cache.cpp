// src/engine/rendering/renderable_gpu_cache.cpp

#include <engine/rendering/renderable_gpu_cache.h>

#include <gpu/mesh.h>
#include <gpu/gaussian_splat.h>
#include <gpu/scalar_field.h>
#include <gpu/vector_field.h>

#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/gaussian_splat_asset_module.h>
#include <engine/assets/gaussian_splat_color_lod_asset_module.h>
#include <engine/assets/scalar_field_asset_module.h>
#include <engine/assets/vector_field_asset_module.h>

namespace wz::engine::rendering
{
    RenderableGpuCache::RenderableGpuCache(
        wz::gpu::DeferredReleaseQueue& release_queue)
        : release_queue_(release_queue)
    {
    }

    const RenderableGpuCache::Entry* RenderableGpuCache::find(
        wz::asset::AssetKey source_asset,
        wz::engine::assets::RenderableKind kind) const
    {
        for (const Entry& entry : entries_) {
            if (entry.kind == kind && entry.source_asset == source_asset)
                return &entry;
        }

        return nullptr;
    }

    void RenderableGpuCache::add(
        wz::asset::AssetKey source_asset,
        wz::engine::assets::RenderableKind kind,
        wz::gpu::ScopedGPUHandle gpu_resource)
    {
        if (!gpu_resource.valid())
            return;

        entries_.push_back(Entry{
            .source_asset = source_asset,
            .kind = kind,
            .gpu_resource = std::move(gpu_resource),
            });
    }

    void RenderableGpuCache::clear()
    {
        // ScopedGPUHandle destructors automatically queue deferred release
        // for all GPU resources — no manual release logic needed here.
        entries_.clear();
        terrain_mesh_chunk_entries_.clear();
        terrain_far_splat_entries_.clear();
    }

    PreparedRenderable RenderableGpuCache::find_mesh_data(
        wz::asset::AssetKey cache_key,
        wz::engine::assets::BuiltinRenderProgram program,
        wz::asset::ResourceHandle render_program,
        wz::engine::assets::RenderDomain domain,
        uint32_t policy_flags) const
    {
        PreparedRenderable out{};
        out.kind = wz::engine::assets::RenderableKind::Mesh;
        out.source_asset = cache_key;
        out.program = program;
        out.render_program = render_program;
        out.domain = domain;
        out.policy_flags = policy_flags;

        if (const Entry* cached =
                find(cache_key, wz::engine::assets::RenderableKind::Mesh))
        {
            out.gpu_resource = cached->gpu_resource.get();
        }

        return out;
    }

    const std::vector<wz::engine::assets::TerrainVisualChunk>*
    RenderableGpuCache::find_terrain_mesh_chunks(
        wz::asset::AssetKey terrain_asset) const
    {
        for (const auto& entry : terrain_mesh_chunk_entries_) {
            if (entry.terrain_asset == terrain_asset) {
                return &entry.chunks;
            }
        }
        return nullptr;
    }

    const std::vector<TerrainTransitionDrawRange>*
    RenderableGpuCache::find_terrain_transition_ranges(
        wz::asset::AssetKey terrain_asset) const
    {
        for (const auto& entry : terrain_mesh_chunk_entries_) {
            if (entry.terrain_asset == terrain_asset) {
                return &entry.transition_ranges;
            }
        }
        return nullptr;
    }

    void RenderableGpuCache::add_terrain_mesh_chunks(
        wz::asset::AssetKey terrain_asset,
        std::vector<wz::engine::assets::TerrainVisualChunk> chunks)
    {
        if (terrain_asset == wz::asset::AssetKey{} || chunks.empty()) {
            return;
        }

        for (auto& entry : terrain_mesh_chunk_entries_) {
            if (entry.terrain_asset == terrain_asset) {
                entry.chunks = std::move(chunks);
                return;
            }
        }

        terrain_mesh_chunk_entries_.push_back(TerrainMeshChunkEntry{
            .terrain_asset = terrain_asset,
            .chunks = std::move(chunks),
            });
    }

    void RenderableGpuCache::add_terrain_transition_ranges(
        wz::asset::AssetKey terrain_asset,
        std::vector<TerrainTransitionDrawRange> ranges)
    {
        if (terrain_asset == wz::asset::AssetKey{}) {
            return;
        }

        for (auto& entry : terrain_mesh_chunk_entries_) {
            if (entry.terrain_asset == terrain_asset) {
                entry.transition_ranges = std::move(ranges);
                return;
            }
        }

        // Transition ranges are sidecar data for terrain mesh chunks.  Do not
        // create an entry that cannot satisfy find_terrain_mesh_chunks().
    }

    const std::vector<TerrainFarSplatChunk>*
    RenderableGpuCache::find_terrain_far_splat_chunks(
        wz::asset::AssetKey terrain_asset) const
    {
        for (const auto& entry : terrain_far_splat_entries_) {
            if (entry.terrain_asset == terrain_asset) {
                return &entry.chunks;
            }
        }
        return nullptr;
    }

    void RenderableGpuCache::add_terrain_far_splat_chunks(
        wz::asset::AssetKey terrain_asset,
        std::vector<TerrainFarSplatChunk> chunks)
    {
        if (terrain_asset == wz::asset::AssetKey{} || chunks.empty()) {
            return;
        }

        TerrainFarSplatEntry entry{};
        entry.terrain_asset = terrain_asset;
        entry.chunks = std::move(chunks);
        // Note: gpu_resources for splat chunks are managed separately by
        // the gpu_scene_render_resource_resolver which stores ScopedGPUHandles
        // for any uploaded splat cloud resources.
        terrain_far_splat_entries_.push_back(std::move(entry));
    }

    PreparedRenderable RenderableGpuCache::realize(
        wz::gpu::Device& device,
        wz::engine::assets::EngineAssetLibrary& assets,
        wz::engine::assets::RenderableHandle handle)
    {
        if (!device.valid())
            return {};

        if (!handle.valid())
            return {};

        const wz::engine::assets::RenderableAssetData* renderable =
            assets.renderables().get_renderable_data(handle);

        if (!renderable || !renderable->valid())
            return {};

        return realize_data(device, assets, *renderable);
    }

    PreparedRenderable RenderableGpuCache::realize_data(
        wz::gpu::Device& device,
        wz::engine::assets::EngineAssetLibrary& assets,
        const wz::engine::assets::RenderableAssetData& renderable)
    {
        if (!device.valid())
            return {};

        if (!renderable.valid())
            return {};

        PreparedRenderable out{};
        out.kind           = renderable.kind;
        out.source_asset   = renderable.source_asset;
        out.program        = renderable.program;
        out.render_program = renderable.render_program;
        out.domain         = renderable.domain;
        out.policy_flags   = renderable.policy_flags;

        if (const Entry* cached = find(renderable.source_asset, renderable.kind)) {
            out.gpu_resource = cached->gpu_resource.get();
            return out;
        }

        switch (renderable.kind)
        {
        case wz::engine::assets::RenderableKind::Mesh:
        {
            const wz::engine::assets::MeshAsset mesh_asset{
                .output = renderable.source_asset,
            };

            const wz::engine::assets::MeshHandle mesh_handle =
                assets.meshes().get_mesh(mesh_asset);

            if (!mesh_handle.valid())
                return {};

            const wz::engine::assets::MeshData* mesh_data =
                assets.meshes().get_mesh_data(mesh_handle);

            if (!mesh_data || !mesh_data->valid())
                return {};

            wz::gpu::ScopedGPUHandle gpu_mesh(
                release_queue_,
                wz::gpu::upload_mesh(device, *mesh_data));

            if (!gpu_mesh.valid())
                return {};

            out.gpu_resource = gpu_mesh.get();
            add(renderable.source_asset, renderable.kind, std::move(gpu_mesh));
            return out;
        }

        case wz::engine::assets::RenderableKind::ScalarField:
        {
            const wz::engine::assets::ScalarFieldAsset scalar_field_asset{
                .output = renderable.source_asset,
            };

            const wz::engine::assets::ScalarFieldHandle scalar_field_handle =
                assets.scalar_fields().get_scalar_field(scalar_field_asset);

            if (!scalar_field_handle.valid())
                return {};

            const wz::engine::assets::ScalarFieldData* scalar_field_data =
                assets.scalar_fields().get_scalar_field_data(scalar_field_handle);

            if (!scalar_field_data || !scalar_field_data->valid())
                return {};

            wz::gpu::ScopedGPUHandle gpu_scalar_field(
                release_queue_,
                wz::gpu::upload_scalar_field_texture(
                    device,
                    *scalar_field_data));

            if (!gpu_scalar_field.valid())
                return {};

            out.gpu_resource = gpu_scalar_field.get();
            add(
                renderable.source_asset,
                renderable.kind,
                std::move(gpu_scalar_field));
            return out;
        }

        case wz::engine::assets::RenderableKind::VectorField:
        {
            const wz::engine::assets::VectorFieldAsset vector_field_asset{
                .output = renderable.source_asset,
            };

            const wz::engine::assets::VectorFieldHandle vector_field_handle =
                assets.vector_fields().get_vector_field(vector_field_asset);

            if (!vector_field_handle.valid())
                return {};

            const wz::engine::assets::VectorFieldData* vector_field_data =
                assets.vector_fields().get_vector_field_data(
                    vector_field_handle);

            if (!vector_field_data || !vector_field_data->valid())
                return {};

            wz::gpu::ScopedGPUHandle gpu_vector_field(
                release_queue_,
                wz::gpu::upload_vector_field_texture(
                    device,
                    *vector_field_data));

            if (!gpu_vector_field.valid())
                return {};

            out.gpu_resource = gpu_vector_field.get();
            add(
                renderable.source_asset,
                renderable.kind,
                std::move(gpu_vector_field));
            return out;
        }

        case wz::engine::assets::RenderableKind::GaussianSplatCloud:
        {
            const wz::engine::assets::GaussianSplatCloudAsset splat_asset{
                .output = renderable.source_asset,
            };

            const wz::engine::assets::GaussianSplatCloudHandle splat_handle =
                assets.gaussian_splats().get_cloud(splat_asset);

            if (!splat_handle.valid())
                return {};

            const wz::engine::assets::GaussianSplatCloudData* splat_data =
                assets.gaussian_splats().get_cloud_data(splat_handle);

            if (!splat_data || !splat_data->valid())
                return {};

            // Optional color LOD companion.  Missing or empty companion key
            // means no LOD blending data — upload uses the safe fallback
            // (lod_color = base_color, confidence = 0).
            const wz::engine::assets::GaussianSplatColorLODData* lod_data = nullptr;

            if (!(renderable.companion_asset == wz::asset::AssetKey{}))
            {
                const wz::engine::assets::GaussianSplatColorLODAsset lod_asset{
                    .output = renderable.companion_asset,
                };
                const wz::engine::assets::GaussianSplatColorLODHandle lod_handle =
                    assets.gaussian_splat_color_lods().get_lod(lod_asset);

                if (lod_handle.valid())
                {
                    lod_data = assets.gaussian_splat_color_lods()
                        .get_lod_data(lod_handle);
                }
            }

            wz::gpu::ScopedGPUHandle gpu_splat_cloud(
                release_queue_,
                lod_data
                    ? wz::gpu::upload_gaussian_splat_cloud(device, *splat_data, *lod_data)
                    : wz::gpu::upload_gaussian_splat_cloud(device, *splat_data));

            if (!gpu_splat_cloud.valid())
                return {};

            out.gpu_resource = gpu_splat_cloud.get();
            add(renderable.source_asset, renderable.kind,
                std::move(gpu_splat_cloud));
            return out;
        }
        }

        return {};
    }

    PreparedRenderable RenderableGpuCache::realize_mesh_data(
        wz::gpu::Device& device,
        wz::asset::AssetKey cache_key,
        const wz::engine::assets::MeshData& mesh,
        wz::engine::assets::BuiltinRenderProgram program,
        wz::asset::ResourceHandle render_program,
        wz::engine::assets::RenderDomain domain,
        uint32_t policy_flags)
    {
        if (!device.valid() || !mesh.valid()) {
            return {};
        }

        PreparedRenderable out{};
        out.kind = wz::engine::assets::RenderableKind::Mesh;
        out.source_asset = cache_key;
        out.program = program;
        out.render_program = render_program;
        out.domain = domain;
        out.policy_flags = policy_flags;

        if (const Entry* cached =
                find(cache_key, wz::engine::assets::RenderableKind::Mesh))
        {
            out.gpu_resource = cached->gpu_resource.get();
            return out;
        }

        wz::gpu::ScopedGPUHandle gpu_mesh(
            release_queue_,
            wz::gpu::upload_mesh(device, mesh));

        if (!gpu_mesh.valid()) {
            return {};
        }

        out.gpu_resource = gpu_mesh.get();
        add(cache_key, wz::engine::assets::RenderableKind::Mesh,
            std::move(gpu_mesh));
        return out;
    }
}
