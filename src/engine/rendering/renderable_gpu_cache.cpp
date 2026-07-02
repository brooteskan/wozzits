// src/engine/rendering/renderable_gpu_cache.cpp

#include <engine/rendering/renderable_gpu_cache.h>

#include <gpu/mesh.h>
#include <gpu/mesh_field_visualization.h>
#include <gpu/gaussian_splat.h>
#include <gpu/scalar_field.h>
#include <gpu/vector_field.h>

#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/mesh_derived_field_asset_module.h>
#include <engine/assets/gaussian_splat_asset_module.h>
#include <engine/assets/gaussian_splat_color_lod_asset_module.h>
#include <engine/assets/scalar_field_asset_module.h>
#include <engine/assets/vector_field_asset_module.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    template <typename Entries, typename Pred>
    auto find_entry(Entries& entries, Pred pred) -> decltype(entries.data())
    {
        const auto it = std::find_if(entries.begin(), entries.end(), pred);
        return it != entries.end() ? &*it : nullptr;
    }

    template <typename Entries>
    auto find_terrain_entry(Entries& entries, wz::asset::AssetKey terrain_asset)
        -> decltype(entries.data())
    {
        return find_entry(entries, [&](const auto& entry) {
            return entry.terrain_asset == terrain_asset;
        });
    }

    uint32_t mesh_mask_channel_set_id(
        const wz::engine::assets::MeshMaskRenderStyleData& mask)
    {
        std::vector<uint32_t> channels;
        channels.reserve(mask.rules.size());
        for (const wz::engine::assets::MeshMaskRule& rule : mask.rules) {
            if (rule.enabled && rule.input_channel_id != 0u) {
                channels.push_back(rule.input_channel_id);
            }
        }
        std::sort(channels.begin(), channels.end());
        channels.erase(
            std::unique(channels.begin(), channels.end()),
            channels.end());
        if (channels.empty()) {
            return 0u;
        }

        uint32_t hash = 2166136261u;
        for (uint32_t channel : channels) {
            for (uint32_t i = 0u; i < 4u; ++i) {
                hash ^= static_cast<uint8_t>((channel >> (i * 8u)) & 0xffu);
                hash *= 16777619u;
            }
        }
        return hash == 0u ? 1u : hash;
    }

    std::vector<uint32_t> mesh_mask_channel_ids(
        const wz::engine::assets::MeshMaskRenderStyleData& mask)
    {
        std::vector<uint32_t> channels;
        channels.reserve(mask.rules.size());
        for (const wz::engine::assets::MeshMaskRule& rule : mask.rules) {
            if (rule.enabled && rule.input_channel_id != 0u) {
                channels.push_back(rule.input_channel_id);
            }
        }
        std::sort(channels.begin(), channels.end());
        channels.erase(
            std::unique(channels.begin(), channels.end()),
            channels.end());
        return channels;
    }

    wz::engine::assets::GpuResidentFieldLayout mesh_mask_field_layout(
        const wz::engine::assets::MeshMaskRenderStyleData& mask)
    {
        return mask.domain == wz::engine::assets::MeshMaskDomain::Vertex
            ? wz::engine::assets::GpuResidentFieldLayout::VertexRaw
            : wz::engine::assets::GpuResidentFieldLayout::FaceRaw;
    }
}

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
        return find_entry(entries_, [&](const Entry& entry) {
            return entry.kind == kind && entry.source_asset == source_asset;
        });
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

    void RenderableGpuCache::swap_with(RenderableGpuCache& other) noexcept
    {
        entries_.swap(other.entries_);
        terrain_mesh_chunk_entries_.swap(other.terrain_mesh_chunk_entries_);
        terrain_far_splat_entries_.swap(other.terrain_far_splat_entries_);
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
        const auto* entry =
            find_terrain_entry(terrain_mesh_chunk_entries_, terrain_asset);
        return entry ? &entry->chunks : nullptr;
    }

    const std::vector<TerrainTransitionDrawRange>*
    RenderableGpuCache::find_terrain_transition_ranges(
        wz::asset::AssetKey terrain_asset) const
    {
        const auto* entry =
            find_terrain_entry(terrain_mesh_chunk_entries_, terrain_asset);
        return entry ? &entry->transition_ranges : nullptr;
    }

    void RenderableGpuCache::add_terrain_mesh_chunks(
        wz::asset::AssetKey terrain_asset,
        std::vector<wz::engine::assets::TerrainVisualChunk> chunks)
    {
        if (terrain_asset == wz::asset::AssetKey{} || chunks.empty()) {
            return;
        }

        if (auto* entry =
                find_terrain_entry(terrain_mesh_chunk_entries_, terrain_asset))
        {
            entry->chunks = std::move(chunks);
            return;
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

        if (auto* entry =
                find_terrain_entry(terrain_mesh_chunk_entries_, terrain_asset))
        {
            entry->transition_ranges = std::move(ranges);
        }

        // Transition ranges are sidecar data for terrain mesh chunks.  Do not
        // create an entry that cannot satisfy find_terrain_mesh_chunks().
    }

    const std::vector<TerrainFarSplatChunk>*
    RenderableGpuCache::find_terrain_far_splat_chunks(
        wz::asset::AssetKey terrain_asset) const
    {
        const auto* entry =
            find_terrain_entry(terrain_far_splat_entries_, terrain_asset);
        return entry ? &entry->chunks : nullptr;
    }

    void RenderableGpuCache::add_terrain_far_splat_chunks(
        wz::asset::AssetKey terrain_asset,
        std::vector<TerrainFarSplatChunk> chunks,
        std::vector<wz::gpu::GPUHandle> gpu_resources)
    {
        if (terrain_asset == wz::asset::AssetKey{} || chunks.empty()) {
            return;
        }

        std::vector<wz::gpu::ScopedGPUHandle> scoped_resources;
        scoped_resources.reserve(gpu_resources.size());
        for (wz::gpu::GPUHandle handle : gpu_resources) {
            if (handle.valid()) {
                scoped_resources.emplace_back(release_queue_, handle);
            }
        }

        if (auto* existing =
                find_terrain_entry(terrain_far_splat_entries_, terrain_asset))
        {
            existing->gpu_resources = std::move(scoped_resources);
            existing->chunks = std::move(chunks);
            return;
        }

        TerrainFarSplatEntry entry{};
        entry.terrain_asset = terrain_asset;
        entry.gpu_resources = std::move(scoped_resources);
        entry.chunks = std::move(chunks);
        terrain_far_splat_entries_.push_back(std::move(entry));
    }

    PreparedRenderable RenderableGpuCache::realize_data(
        wz::gpu::Device& device,
        wz::engine::assets::EngineAssetLibrary& assets,
        const wz::engine::assets::RenderableAssetData& renderable)
    {
        auto fail = [&](const char* reason)
        {
            assets.logger().error(
                std::string("renderable gpu cache realize_data failed: ")
                + reason);
            return PreparedRenderable{};
        };

        if (!device.valid())
            return {};

        if (!renderable.valid())
            return fail("renderable data is invalid");

        PreparedRenderable out{};
        out.kind           = renderable.kind;
        out.source_asset   = renderable.source_asset;
        out.program        = renderable.program;
        out.render_program = renderable.render_program;
        out.domain         = renderable.domain;
        out.policy_flags   = renderable.policy_flags;

        if (const Entry* cached = find(renderable.source_asset, renderable.kind)) {
            out.gpu_resource = cached->gpu_resource.get();
            if (renderable.kind != wz::engine::assets::RenderableKind::Mesh
                || renderable.mesh_field_visualization_asset
                    == wz::asset::AssetKey{})
            {
                return out;
            }
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
                return fail("mesh asset handle not found");

            const wz::engine::assets::MeshData* mesh_data =
                assets.meshes().get_mesh_data(mesh_handle);

            if (!mesh_data || !mesh_data->valid())
                return fail("mesh asset data is missing or invalid");

            if (!out.gpu_resource.valid()) {
                wz::gpu::ScopedGPUHandle gpu_mesh(
                    release_queue_,
                    wz::gpu::upload_mesh(device, *mesh_data));

                if (!gpu_mesh.valid())
                    return fail("GPU mesh upload failed");

                out.gpu_resource = gpu_mesh.get();
                add(renderable.source_asset, renderable.kind, std::move(gpu_mesh));
            }

            if (!(renderable.mesh_field_visualization_asset
                    == wz::asset::AssetKey{}))
            {
                const bool wants_mask =
                    renderable.program
                        == wz::engine::assets::BuiltinRenderProgram::
                            MeshMaskStyle
                    && renderable.mesh_style.mask.enabled;
                const uint32_t channel_id = wants_mask
                    ? mesh_mask_channel_set_id(renderable.mesh_style.mask)
                    : renderable.mesh_style.field_visualization.channel_id;
                const wz::engine::assets::GpuResidentFieldLayout layout =
                    wants_mask
                        ? mesh_mask_field_layout(renderable.mesh_style.mask)
                        : wz::engine::assets::GpuResidentFieldLayout::
                            VertexProjected;
                if (channel_id == 0u) {
                    return out;
                }
                // The GPU-resident field table is the single owner of mesh
                // field visualization resources. Behavior compute refreshes
                // resident entries in place, so renderables must bind the
                // resident resource — a private copy would keep showing the
                // initial data forever.
                if (const wz::gpu::GPUHandle resident_field =
                        assets.gpu_resident_fields().find(
                            renderable.mesh_field_visualization_asset,
                            channel_id,
                            layout);
                    resident_field.valid())
                {
                    out.mesh_field_visualization_resource = resident_field;
                }
                else {
                    const wz::engine::assets::MeshDerivedFieldAsset field_asset{
                        .output = renderable.mesh_field_visualization_asset,
                    };
                    const wz::engine::assets::MeshDerivedFieldHandle field_handle =
                        assets.mesh_derived_fields().get_mesh_derived_field(
                            field_asset);
                    if (!field_handle.valid()) {
                        if (wants_mask) {
                            return out;
                        }
                        return fail(
                            "mesh field visualization asset handle not found");
                    }

                    const wz::engine::assets::MeshDerivedFieldData* field_data =
                        assets.mesh_derived_fields()
                            .get_mesh_derived_field_data(field_handle);
                    if (!field_data || !field_data->valid()) {
                        if (wants_mask) {
                            return out;
                        }
                        return fail(
                            "mesh field visualization data is missing or invalid");
                    }

                    wz::gpu::GPUHandle gpu_field{};
                    if (wants_mask) {
                        const std::vector<uint32_t> channels =
                            mesh_mask_channel_ids(renderable.mesh_style.mask);
                        if (channels.empty()) {
                            return out;
                        }
                        if (renderable.mesh_style.mask.domain
                            == wz::engine::assets::MeshMaskDomain::Vertex)
                        {
                            gpu_field = wz::gpu::upload_mesh_field_raw_vertices(
                                device,
                                wz::gpu::MeshFieldRawVertexUploadDesc{
                                    .mesh = mesh_data,
                                    .field = field_data,
                                    .channel_ids = channels.data(),
                                    .channel_count =
                                        static_cast<uint32_t>(channels.size()),
                                });
                        }
                        else {
                            gpu_field = wz::gpu::upload_mesh_field_raw_faces(
                                device,
                                wz::gpu::MeshFieldRawFaceUploadDesc{
                                    .mesh = mesh_data,
                                    .field = field_data,
                                    .channel_ids = channels.data(),
                                    .channel_count =
                                        static_cast<uint32_t>(channels.size()),
                                });
                        }
                    }
                    else {
                        gpu_field = wz::gpu::upload_mesh_field_visualization(
                            device,
                            wz::gpu::MeshFieldVisualizationUploadDesc{
                                .mesh = mesh_data,
                                .field = field_data,
                                .channel_id = channel_id,
                            });
                    }
                    if (!gpu_field.valid()) {
                        if (wants_mask) {
                            return out;
                        }
                        return fail("mesh field visualization upload failed");
                    }

                    if (!assets.gpu_resident_fields().add(
                            wz::engine::assets::GpuResidentFieldEntry{
                                .field_key =
                                    renderable.mesh_field_visualization_asset,
                                .channel_id = channel_id,
                                .layout = layout,
                                .gpu_resource = gpu_field,
                            }))
                    {
                        wz::gpu::release_mesh_field_visualization(
                            device,
                            gpu_field);
                        if (wants_mask) {
                            return out;
                        }
                        return fail(
                            "mesh field visualization resident resource registration failed");
                    }
                    out.mesh_field_visualization_resource = gpu_field;
                }
            }
            return out;
        }

        // ScalarField / VectorField now fall through to the shared `return {}`
        // below — their realize_data bodies were removed (issue #139, via #195
        // slice E). Kept as explicit empty cases so the switch stays exhaustive
        // over RenderableKind (no -Wswitch warning) while documenting that these
        // kinds are intentionally not realized here.
        //
        // These were the ONLY reachable callers of upload_scalar_field_texture /
        // These were the ONLY reachable callers of upload_scalar_field_texture /
        // upload_vector_field_texture, but they were themselves DEAD: the sole
        // caller of realize_data (GpuSceneRenderResourceResolver::
        // realize_renderable_descriptor) only ever passes RenderableKind::Mesh
        // here — a ScalarField renderable is diverted to the resolver's own
        // preview-mesh branch (make_heightfield_preview_mesh) and a VectorField
        // renderable hits the resolver's `return false`, so neither ScalarField
        // nor VectorField ever reaches this switch. Deleting these two cases
        // removes the last live references to the field-texture upload path. The
        // upload functions + DX12ScalarFieldTextureTable are NOT deleted here:
        // the table is still a compile-time dependency of the live sky-render
        // path (dx12_submit.cpp) and the test asset_scalar_field_texture_test,
        // so that removal belongs to a broader field-texture subsystem retirement
        // (see the #195 report). The Mesh case (below) is untouched — it is the
        // only live realize_data path.
        case wz::engine::assets::RenderableKind::ScalarField:
        case wz::engine::assets::RenderableKind::VectorField:
            return {};

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
