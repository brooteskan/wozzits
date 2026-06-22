// src/engine/rendering/rhi_scene_renderer.cpp
//
// The RHI scene render path, lifted from the scene editor's working
// render_scene_rhi / ensure_rhi_renderable / realize_rhi_program so the app and
// editor share one wozzits-rhi implementation (no legacy dx12 submit).

#include <engine/rendering/rhi_scene_renderer.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/mesh/mesh.h>
#include <engine/assets/rhi_asset_identity.h>
#include <engine/assets/render_program/render_program.h>
#include <engine/assets/renderable/renderable.h>
#include <engine/assets/scene/scene_asset_data.h>

#include <engine/rendering/rhi_mesh_bridge.h>
#include <engine/rendering/rhi_render_program_bridge.h>
#include <engine/rendering/rhi_shader_bridge.h>

#include <gpu/dx12/dx12_internal.h>

#include <math/mat4.h>

#include <wozzits/rhi/draw_encode.h>
#include <wozzits/rhi/geometry_view.h>
#include <wozzits/rhi/shader_module.h>
#include <wozzits/rhi/shader_resource_group_layout.h>

#include <d3d12.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ea = wz::engine::assets;

namespace wz::engine::rendering
{
    namespace
    {
        std::vector<float> tight_mesh_positions(const ea::MeshData& mesh)
        {
            std::vector<float> out;
            out.reserve(mesh.vertices.size() * 3u);
            for (const ea::MeshVertex& vertex : mesh.vertices) {
                out.push_back(vertex.position[0]);
                out.push_back(vertex.position[1]);
                out.push_back(vertex.position[2]);
            }
            return out;
        }

        void release_unrealized_pull_buffers(
            wz::rhi::GpuResourceRegistry& resources,
            wz::rhi::GpuResourceHandle positions,
            wz::rhi::GpuResourceHandle indices)
        {
            if (positions.valid()) {
                resources.release(positions);
            }
            if (indices.valid()) {
                resources.release(indices);
            }
            // No collect() here. This runs mid-frame, with no wait_idle, while
            // other renderables may still reference resources the GPU has not
            // finished. collect(UINT64_MAX) ignores the last-use timeline and
            // would destroy them immediately (the registry's timeline is not
            // wired — nothing calls touch() — so even collect(current) would be
            // unsafe). Worse, pull buffers are deduped by identity without
            // refcounting, so the handle released here may be shared with a
            // live renderable. release() alone is timeline-safe: it drops these
            // from identity lookup so a fresh realize rebuilds, and the buffers
            // are reclaimed by on_graph_changed's wait_idle-guarded collect.
        }

        const wz::asset::AssetSystem::RegistrationEntry* registration_entry_for(
            const ea::EngineAssetLibrary& assets, const wz::asset::AssetKey& key)
        {
            const auto registered_assets = assets.system().registered_assets();
            const auto entry = std::ranges::find_if(
                registered_assets,
                [&key](const wz::asset::AssetSystem::RegistrationEntry& candidate) {
                    return candidate.node.key == key;
                });
            return entry != registered_assets.end() ? &*entry : nullptr;
        }

        struct PullMeshSource
        {
            wz::asset::AssetKey mesh_key{};
            wz::asset::AssetKey program_key{};
            uint64_t buffer_identity = 0;

            // Set when the asset compiler has published GPU-resident pull buffers
            // for this source (gpu_sparse_mesh). The renderer binds those by the
            // identity the compiler used — rhi_asset_identity(resident_key,
            // "pull_positions"/"pull_indices") — instead of re-uploading CPU mesh
            // data, with counts from the resident asset (no CPU MeshData needed).
            std::optional<wz::asset::AssetKey> resident_key{};
            uint32_t                           vertex_count = 0;
            uint32_t                           index_count = 0;
        };

        std::optional<PullMeshSource> pull_mesh_source_for_renderable(
            const ea::EngineAssetLibrary& assets,
            const wz::asset::AssetKey& renderable_key)
        {
            if (const ea::RhiRenderableRecipe* recipe =
                    assets.renderables().get_rhi_renderable_recipe(
                        ea::RenderableAsset{ .output = renderable_key }))
            {
                return PullMeshSource{
                    .mesh_key = recipe->mesh_key,
                    .program_key = recipe->program_key,
                    .buffer_identity = ea::rhi_asset_identity(recipe->mesh_key),
                };
            }

            const ea::RenderableHandle renderable_handle =
                assets.renderables().get_renderable(
                    ea::RenderableAsset{ .output = renderable_key });
            const ea::RenderableAssetData* renderable =
                assets.renderables().get_renderable_data(renderable_handle);
            if (!renderable
                || renderable->kind != ea::RenderableKind::Mesh
                || !renderable->render_program.valid())
            {
                return std::nullopt;
            }

            const wz::asset::AssetSystem::RegistrationEntry* entry =
                registration_entry_for(assets, renderable_key);
            if (!entry || entry->dep_keys.size() < 2u) {
                return std::nullopt;
            }

            const ea::GpuSparseMeshHandle sparse_handle =
                assets.gpu_sparse_meshes().get_gpu_sparse_mesh(
                    ea::GpuSparseMeshAsset{ .output = renderable->source_asset });
            const ea::GpuSparseMeshData* sparse =
                assets.gpu_sparse_meshes().get_gpu_sparse_mesh_data(sparse_handle);
            if (!sparse || !sparse->valid()) {
                return std::nullopt;
            }

            return PullMeshSource{
                .mesh_key = sparse->source_mesh_key,
                .program_key = entry->dep_keys[1],
                .buffer_identity =
                    ea::rhi_asset_identity(renderable->source_asset),
                .resident_key = renderable->source_asset,
                .vertex_count = sparse->vertex_count,
                .index_count = sparse->index_count,
            };
        }

        // Compose an authored TRS into a column-major world matrix (translation
        // in m[12..14]), matching the MVP convention the pull program expects.
        wz::math::Mat4 world_from_transform(const ea::AuthoredTransform& t)
        {
            const float x = t.rotation_quat[0];
            const float y = t.rotation_quat[1];
            const float z = t.rotation_quat[2];
            const float w = t.rotation_quat[3];
            const float r00 = 1.0f - 2.0f * (y * y + z * z);
            const float r01 = 2.0f * (x * y - w * z);
            const float r02 = 2.0f * (x * z + w * y);
            const float r10 = 2.0f * (x * y + w * z);
            const float r11 = 1.0f - 2.0f * (x * x + z * z);
            const float r12 = 2.0f * (y * z - w * x);
            const float r20 = 2.0f * (x * z - w * y);
            const float r21 = 2.0f * (y * z + w * x);
            const float r22 = 1.0f - 2.0f * (x * x + y * y);

            wz::math::Mat4 m{};
            m.m[0] = r00 * t.scale[0];
            m.m[1] = r10 * t.scale[0];
            m.m[2] = r20 * t.scale[0];
            m.m[3] = 0.0f;
            m.m[4] = r01 * t.scale[1];
            m.m[5] = r11 * t.scale[1];
            m.m[6] = r21 * t.scale[1];
            m.m[7] = 0.0f;
            m.m[8] = r02 * t.scale[2];
            m.m[9] = r12 * t.scale[2];
            m.m[10] = r22 * t.scale[2];
            m.m[11] = 0.0f;
            m.m[12] = t.translation[0];
            m.m[13] = t.translation[1];
            m.m[14] = t.translation[2];
            m.m[15] = 1.0f;
            return m;
        }
    }

    RhiSceneRenderer::RhiSceneRenderer(EngineGpuContext& gpu, wz::Logger& logger)
        : gpu_(gpu)
        , logger_(logger)
        , ctx_()
        , cache_(gpu.device, gpu.programs, gpu.compute_programs, gpu.shaders)
        , recorder_(gpu.device, cache_, gpu.resources, gpu.backend)
    {
        forward_ = ctx_.passes.acquire("forward");
    }

    void RhiSceneRenderer::simulation_tick()
    {
    }

    void RhiSceneRenderer::on_graph_changed()
    {
        // The new graph may fix previously-broken renderables; always allow
        // re-realize (must happen before the early-return below, since a fully
        // broken outgoing graph realizes nothing yet still populated this set).
        failed_renderables_.clear();

        // First bind (nothing realized yet): nothing to release or invalidate,
        // and no need to flush the GPU. Keeps load_scene's initial bind cheap.
        if (realized_renderables_.empty() && realized_programs_.empty()
            && registered_shaders_.empty()
            && gpu_.resources.resident_count() == 0u)
        {
            return;
        }

        // The outgoing graph's pull buffers may still be referenced by frames
        // the GPU has not finished. The registry's deferred release reclaims on
        // a GPU timeline value, but that timeline is not yet wired (nothing
        // calls touch()), so flush here to guarantee the GPU is done before we
        // collect — then release + collect is unambiguously safe.
        wz::gpu::wait_idle(gpu_.device);

        for (auto& [key, renderable] : realized_renderables_) {
            (void)key;
            // Only release renderer-owned (CPU-upload) buffers. Resident
            // asset-published buffers are owned by the asset library and
            // released by its release_unregistered_rhi_resources on the same
            // swap (deferred, before this collect) — never by the renderer.
            if (!renderable.owns_buffers) {
                continue;
            }
            if (renderable.positions.valid()) {
                gpu_.resources.release(renderable.positions);
            }
            if (renderable.indices.valid()) {
                gpu_.resources.release(renderable.indices);
            }
        }

        // The GPU is idle after wait_idle, so every pending resource is past its
        // last-use timeline: reclaim them all now (UINT64_MAX = "all timelines
        // completed"). Without this the released buffers would linger resident.
        gpu_.resources.collect(UINT64_MAX);

        // Release the realized PSOs + root signatures. These are keyed by rhi
        // program Tag; leaving them resident leaks device objects across swaps,
        // and a re-realize must rebuild them against the new graph anyway.
        cache_.clear();

        // Release the recorder's cached SRV descriptor tables. They view the
        // outgoing graph's pull buffers (just released); keeping them leaks
        // descriptor-heap ranges across swaps and could reuse a table for a
        // recycled handle. Safe now: wait_idle above flushed the GPU.
        recorder_.release_cached_descriptor_tables();

        // NOTE: the shared rhi program / compute-program / shader registries are
        // intentionally NOT cleared here. They live on EngineGpuContext and the
        // asset compiler registers into them during resolve — which runs BEFORE
        // on_graph_changed in the bind path — so clearing here would wipe the
        // program the compiler just produced. The asset side owns their lifecycle
        // now and clears them before resolve re-registers
        // (EngineAssetLibrary::reset_rhi_render_program_registries). The semantic
        // registries are graph-independent and kept across swaps.

        realized_renderables_.clear();
        realized_programs_.clear();
        registered_shaders_.clear();
    }

    bool RhiSceneRenderer::register_shader_from_source(
        ea::EngineAssetLibrary& assets,
        const wz::asset::AssetKey& shader_key,
        wz::rhi::ShaderStage stage,
        const char* target)
    {
        if (auto it = registered_shaders_.find(shader_key);
            it != registered_shaders_.end())
        {
            return it->second;
        }

        // Find-then-fallback: the asset compiler may have already produced this
        // shader module during resolve. If so, skip the render-time D3DCompile.
        if (gpu_.shaders.find(wz::engine::rendering::shader_ref(shader_key))
                .valid())
        {
            registered_shaders_[shader_key] = true;
            return true;
        }

        const auto source = assets.rhi_shader_source(shader_key);
        if (!source) {
            registered_shaders_[shader_key] = false;
            return false;
        }
        const auto bytecode = wz::engine::rendering::compile_hlsl_bytecode(
            source->bytes, source->entry, target, logger_);
        if (!bytecode) {
            registered_shaders_[shader_key] = false;
            return false;
        }
        const wz::rhi::Tag tag = gpu_.shaders.register_program(
            wz::rhi::ShaderModuleDesc{
                wz::engine::rendering::shader_ref(shader_key), stage, *bytecode });
        const bool ok = tag.valid();
        registered_shaders_[shader_key] = ok;
        return ok;
    }

    const RhiSceneRenderer::RealizedProgram* RhiSceneRenderer::realize_program(
        ea::EngineAssetLibrary& assets, const wz::asset::AssetKey& program_key)
    {
        if (auto it = realized_programs_.find(program_key);
            it != realized_programs_.end())
        {
            return it->second.program.valid() ? &it->second : nullptr;
        }

        // Find-then-fallback: prefer the rhi program the asset compiler produced
        // (registered under program_ref during resolve). Binding it skips the
        // render-time read-legacy / D3DCompile / convert / register path.
        const wz::rhi::Tag produced =
            gpu_.programs.find(wz::engine::rendering::program_ref(program_key));
        if (produced.valid()) {
            if (!cache_.realize(produced)) {
                logger_.error("RhiSceneRenderer: pipeline realize failed");
                return nullptr;
            }
            auto [it, inserted] = realized_programs_.try_emplace(program_key);
            it->second = RealizedProgram{ program_key, {}, {}, produced };
            (void)inserted;
            return &it->second;
        }

        // Fallback: bridge the legacy program at render time — builtin programs,
        // not-yet-migrated custom programs, or a custom program whose compile was
        // a cache hit on rebind (so the producer's lambda didn't re-register it).
        ++render_time_program_bridges_;

        const auto* program_entry = registration_entry_for(assets, program_key);
        if (!program_entry || program_entry->dep_keys.size() < 2u) {
            logger_.error("RhiSceneRenderer: program missing shader deps");
            return nullptr;
        }
        const wz::asset::AssetKey vertex_key = program_entry->dep_keys[0];
        const wz::asset::AssetKey pixel_key = program_entry->dep_keys[1];

        const wz::asset::ResourceHandle program_handle =
            assets.render_programs().get_render_program(
                ea::RenderProgramAsset{ .key = program_key });
        const ea::RenderProgramData* data =
            assets.render_programs().get_render_program_data(program_handle);
        if (!data) {
            logger_.error("RhiSceneRenderer: render program data unavailable");
            return nullptr;
        }

        if (!register_shader_from_source(
                assets, vertex_key, wz::rhi::ShaderStage::Vertex, "vs_5_1")
            || !register_shader_from_source(
                assets, pixel_key, wz::rhi::ShaderStage::Pixel, "ps_5_1"))
        {
            return nullptr;
        }

        const auto converted = wz::engine::rendering::to_rhi_render_program_desc(
            *data, program_key, vertex_key, pixel_key,
            gpu_.descriptor_semantics, gpu_.constant_semantics);
        if (!converted) {
            logger_.error("RhiSceneRenderer: program bridge failed");
            return nullptr;
        }

        const wz::rhi::Tag tag = gpu_.programs.register_program(*converted);
        if (!tag.valid() || !cache_.realize(tag)) {
            logger_.error("RhiSceneRenderer: pipeline realize failed");
            return nullptr;
        }

        auto [it, inserted] = realized_programs_.try_emplace(program_key);
        it->second = RealizedProgram{ program_key, vertex_key, pixel_key, tag };
        (void)inserted;
        return &it->second;
    }

    RhiSceneRenderer::RealizedRenderable* RhiSceneRenderer::ensure_renderable(
        ea::EngineAssetLibrary& assets, const wz::asset::AssetKey& renderable_key)
    {
        if (auto it = realized_renderables_.find(renderable_key);
            it != realized_renderables_.end())
        {
            return &it->second;
        }
        // Already known-unrealizable for this graph: skip silently (no per-frame
        // retry/log). Cleared on the next graph swap.
        if (failed_renderables_.count(renderable_key) != 0u) {
            return nullptr;
        }

        const auto source = pull_mesh_source_for_renderable(assets, renderable_key);
        if (!source) {
            failed_renderables_.insert(renderable_key);
            return nullptr;
        }
        const RealizedProgram* program = realize_program(assets, source->program_key);
        if (!program) {
            failed_renderables_.insert(renderable_key);
            return nullptr;
        }

        const wz::rhi::RenderProgramDesc* registered =
            gpu_.programs.get(program->program);
        const wz::rhi::ShaderResourceGroupLayout* slot2_layout =
            registered
                ? wz::rhi::find_shader_resource_group_layout(
                    registered->shader_resource_groups, 2)
                : nullptr;
        if (!slot2_layout) {
            logger_.error("RhiSceneRenderer: program missing object SRG slot 2");
            failed_renderables_.insert(renderable_key);
            return nullptr;
        }

        // Prefer the asset-published GPU-resident pull buffers (gpu_sparse_mesh),
        // found by the identity the compiler used. Those are asset-owned, so the
        // renderer binds but never releases them. Fall back to a per-realize CPU
        // upload for sources without resident buffers (e.g. the rhi pull-mesh
        // recipe) — those the renderer owns and releases.
        wz::rhi::GpuResourceHandle positions_handle{};
        wz::rhi::GpuResourceHandle indices_handle{};
        bool owns_buffers = false;
        uint32_t index_count = 0;
        uint32_t vertex_count = 0;

        if (source->resident_key) {
            positions_handle = gpu_.resources.find(wz::rhi::ResourceIdentity{
                ea::rhi_asset_identity(*source->resident_key, "pull_positions"),
                {} });
            indices_handle = gpu_.resources.find(wz::rhi::ResourceIdentity{
                ea::rhi_asset_identity(*source->resident_key, "pull_indices"),
                {} });
        }

        if (positions_handle.valid() && indices_handle.valid()) {
            // Resident path: bind asset-owned buffers; counts from the asset.
            index_count = source->index_count;
            vertex_count = source->vertex_count;
        }
        else {
            // CPU-upload fallback: renderer-owned buffers.
            const ea::MeshHandle mesh_handle = assets.meshes().get_mesh(
                ea::MeshAsset{ .output = source->mesh_key });
            const ea::MeshData* mesh =
                assets.meshes().get_mesh_data(mesh_handle);
            if (!mesh || !mesh->valid()) {
                logger_.error("RhiSceneRenderer: renderable mesh unavailable");
                failed_renderables_.insert(renderable_key);
                return nullptr;
            }
            const wz::rhi::Tag position_variant =
                ctx_.resource_variants.acquire("mesh.pull_positions");
            const wz::rhi::Tag index_variant =
                ctx_.resource_variants.acquire("mesh.pull_indices");
            const std::vector<float> positions = tight_mesh_positions(*mesh);
            positions_handle = acquire_pull_buffer(
                gpu_.resources, source->buffer_identity, position_variant,
                positions.data(), positions.size() * sizeof(float),
                3u * sizeof(float));
            indices_handle = acquire_pull_buffer(
                gpu_.resources, source->buffer_identity, index_variant,
                mesh->indices.data(),
                mesh->indices.size() * sizeof(uint32_t),
                sizeof(uint32_t));
            if (!positions_handle.valid() || !indices_handle.valid()) {
                logger_.error("RhiSceneRenderer: pull buffer upload failed");
                release_unrealized_pull_buffers(
                    gpu_.resources, positions_handle, indices_handle);
                failed_renderables_.insert(renderable_key);
                return nullptr;
            }
            owns_buffers = true;
            index_count = mesh->index_count();
            vertex_count = mesh->vertex_count();
        }

        auto [it, inserted] = realized_renderables_.try_emplace(renderable_key);
        RealizedRenderable& realized = it->second;
        realized.renderable_key = renderable_key;
        realized.program = program->program;
        realized.positions = positions_handle;
        realized.indices = indices_handle;
        realized.owns_buffers = owns_buffers;
        realized.object_srg.reset(*slot2_layout);

        const wz::rhi::Tag pulled_positions =
            gpu_.descriptor_semantics.find("pulled_mesh_positions");
        const wz::rhi::Tag pulled_indices =
            gpu_.descriptor_semantics.find("pulled_mesh_indices");
        if (!realized.object_srg.set(pulled_positions, realized.positions)
            || !realized.object_srg.set(pulled_indices, realized.indices)
            || !realized.object_srg.satisfies(*slot2_layout))
        {
            if (realized.owns_buffers) {
                release_unrealized_pull_buffers(
                    gpu_.resources, realized.positions, realized.indices);
            }
            realized_renderables_.erase(it);
            logger_.error("RhiSceneRenderer: object SRG build failed");
            failed_renderables_.insert(renderable_key);
            return nullptr;
        }

        wz::rhi::GeometryView geometry;
        geometry.index_buffer = realized.indices;
        geometry.index_count = index_count;
        geometry.vertex_count = vertex_count;

        const wz::math::Mat4 initial_mvp = wz::math::Mat4::identity();
        wz::rhi::DrawPacketAllocator allocator;
        wz::rhi::DrawPacketBuilder builder =
            wz::rhi::DrawPacketBuilder::begin(allocator);
        builder
            .set_geometry(geometry)
            .set_root_constants(std::span<const uint8_t>{
                reinterpret_cast<const uint8_t*>(initial_mvp.m),
                sizeof(initial_mvp.m) })
            .add_shader_resource_group(realized.object_srg);
        if (!builder.add_draw_item(wz::rhi::DrawRequest{
                forward_, realized.program, nullptr,
                wz::rhi::StreamBufferIndices{}, 0,
                wz::rhi::DrawListMask::from(forward_) }))
        {
            if (realized.owns_buffers) {
                release_unrealized_pull_buffers(
                    gpu_.resources, realized.positions, realized.indices);
            }
            realized_renderables_.erase(it);
            logger_.error("RhiSceneRenderer: draw packet build failed");
            failed_renderables_.insert(renderable_key);
            return nullptr;
        }
        realized.packet = builder.end();

        (void)inserted;
        return &realized;
    }

    bool RhiSceneRenderer::render_scene(
        std::span<const ea::SceneNodeAsset> nodes,
        ea::EngineAssetLibrary& assets,
        const wz::math::Mat4& view_projection)
    {
        ID3D12GraphicsCommandList* cmd =
            wz::gpu::dx12::internal::get_command_list(gpu_.device);
        if (!cmd) {
            return false;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE rtv =
            wz::gpu::dx12::internal::get_current_rtv(gpu_.device);
        D3D12_CPU_DESCRIPTOR_HANDLE dsv =
            wz::gpu::dx12::internal::get_dsv(gpu_.device);
        cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

        const float w =
            static_cast<float>(wz::gpu::dx12::internal::get_width(gpu_.device));
        const float h =
            static_cast<float>(wz::gpu::dx12::internal::get_height(gpu_.device));
        D3D12_VIEWPORT viewport{ 0.0f, 0.0f, w, h, 0.0f, 1.0f };
        cmd->RSSetViewports(1, &viewport);
        D3D12_RECT scissor{ 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };
        cmd->RSSetScissorRects(1, &scissor);

        uint32_t recorded = 0;
        for (const ea::SceneNodeAsset& node : nodes) {
            if (!node.visible || !node.renderable_asset) {
                continue;
            }
            RealizedRenderable* realized =
                ensure_renderable(assets, *node.renderable_asset);
            if (!realized) {
                continue;
            }

            const wz::math::Mat4 world = world_from_transform(node.local);
            const wz::math::Mat4 mvp = wz::math::mul(view_projection, world);
            realized->packet.root_constants.assign(
                reinterpret_cast<const uint8_t*>(mvp.m),
                reinterpret_cast<const uint8_t*>(mvp.m) + sizeof(mvp.m));

            wz::rhi::record_packet(realized->packet, forward_, recorder_);
            ++recorded;
        }

        if (recorded == 0) {
            return true;
        }
        if (!recorder_.ready()) {
            logger_.error("RhiSceneRenderer: recorder rejected a draw");
            return false;
        }
        return true;
    }
}
