// file: src/engine/render_backends/dx12/dx12_submit.cpp

#include <d3d12.h>

#include <engine/render_backends/dx12/dx12_submit.h>
#include <gpu/dx12/dx12_internal.h>

#include <iostream>

namespace wz::render::backend::dx12
{

    Context* create(
    wz::gpu::Device& device,
    const TrianglePipelineDesc& tri_desc)
{
    assert(tri_desc.valid());

    ID3D12Device* dev = wz::gpu::dx12::internal::get_device(device);

    Context* ctx = new Context();
    ctx->device = &device;

    ctx->root_sig =
        wz::gpu::dx12::internal::create_empty_root_signature(dev);

    ctx->pso = wz::gpu::dx12::internal::create_triangle_pso(
        device,
        ctx->root_sig,
        tri_desc.vertex_shader,
        tri_desc.pixel_shader
    );



        char buf[128];
        sprintf_s(buf, "  PSO created: %p\n", ctx->pso);
        OutputDebugStringA(buf);
        assert(ctx->pso);

        struct Vertex { float x, y, z; };

        Vertex tri[3] =
        {
            {  0.0f,  0.5f, 0.0f },
            {  0.5f, -0.5f, 0.0f },
            { -0.5f, -0.5f, 0.0f }
        };

        const UINT vb_size = sizeof(tri);

        // ────── vertex buffer ──────
        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC vb_desc = {};
        vb_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        vb_desc.Width = vb_size;
        vb_desc.Height = 1;
        vb_desc.DepthOrArraySize = 1;
        vb_desc.MipLevels = 1;
        vb_desc.SampleDesc.Count = 1;
        vb_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = dev->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &vb_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&ctx->vertex_buffer)
        );
        assert(SUCCEEDED(hr));

        void* mapped = nullptr;
        ctx->vertex_buffer->Map(0, nullptr, &mapped);
        memcpy(mapped, tri, vb_size);
        ctx->vertex_buffer->Unmap(0, nullptr);

        ctx->vb_view.BufferLocation = ctx->vertex_buffer->GetGPUVirtualAddress();
        ctx->vb_view.StrideInBytes = sizeof(Vertex);
        ctx->vb_view.SizeInBytes = vb_size;

        // ────── mesh table ──────
        ctx->mesh_table.resize(1);

        GpuMesh mesh{};
        mesh.vertex_buffer = ctx->vertex_buffer;
        mesh.index_buffer = nullptr;          // IMPORTANT: no IB yet
        mesh.vb_view = ctx->vb_view;
        mesh.ib_view = {};                    // unused
        mesh.index_count = 3;

        ctx->mesh_table[0] = mesh;

        assert(ctx->root_sig);
        assert(ctx->pso);
        assert(ctx->vertex_buffer);


        return ctx;
    }

    void submit(Context* ctx, const RenderFrameView& frame)
    {
        assert(ctx);
        assert(ctx->device);
        assert(ctx->device->impl);

        //{
        //    char buf[128];
        //    sprintf_s(
        //        buf,
        //        "RenderFrame submit: commands=%zu\n",
        //        frame.commands.size()
        //    );
        //    OutputDebugStringA(buf);
        //}

        auto* cmdList =
            wz::gpu::dx12::internal::get_command_list(*ctx->device);

        cmdList->SetGraphicsRootSignature(ctx->root_sig);
        cmdList->SetPipelineState(ctx->pso);

        cmdList->IASetPrimitiveTopology(
            D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
        );

        struct
        {
            Mat4 world;
            Mat4 view_proj;
        } data;

        for (const DrawCommand& dc : frame.opaque)
        {


            if (dc.mesh >= ctx->mesh_table.size())
                continue;

            const auto& mesh = ctx->mesh_table[dc.mesh];

            cmdList->IASetVertexBuffers(0, 1, &mesh.vb_view);

            data.world = dc.world;
            data.view_proj = frame.view.view_projection;

            cmdList->SetGraphicsRoot32BitConstants(
                0,
                32,
                &data,
                0
            );

            if (mesh.index_buffer)
            {
                cmdList->IASetIndexBuffer(&mesh.ib_view);
                cmdList->DrawIndexedInstanced(mesh.index_count, 1, 0, 0, 0);
            }
            else
            {
                OutputDebugStringA("Drawing opaque debug mesh\n");
                cmdList->DrawInstanced(mesh.index_count, 1, 0, 0);
            }
        }
    }

    void submit(wz::gpu::Device& device,
                const RenderFrameView& frame,
                const wz::engine::rendering::RenderResourceResolver& resolver)
    {
        auto* cmdList = wz::gpu::dx12::internal::get_command_list(device);

        // ── Opaque mesh pass (resolver path) ──────────────────────────────────

        if (!frame.opaque.empty())
        {
            const auto mesh_pipeline =
                wz::gpu::dx12::internal::get_mesh_wireframe_pipeline(device);

            if (mesh_pipeline.valid())
            {
                cmdList->SetGraphicsRootSignature(mesh_pipeline.root_sig);
                cmdList->SetPipelineState(mesh_pipeline.pso);
                cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                struct { Mat4 world; Mat4 view_proj; } constants;

                for (const DrawCommand& dc : frame.opaque)
                {
                    if (dc.kind != DrawCommandKind::Mesh)
                        continue;
                    if (dc.mesh == INVALID_MESH)
                        continue;

                    const auto resolved = resolver.resolve_mesh(dc.mesh);
                    if (!resolved)
                        continue;

                    const auto* mesh =
                        wz::gpu::dx12::internal::get_mesh(device, resolved->gpu_resource);
                    if (!mesh || !mesh->vertex_buffer)
                        continue;

                    constants.world     = dc.world;
                    constants.view_proj = frame.view.view_projection;

                    cmdList->SetGraphicsRoot32BitConstants(0, 32, &constants, 0);
                    cmdList->IASetVertexBuffers(0, 1, &mesh->vertex_view);
                    cmdList->IASetIndexBuffer(&mesh->index_view);
                    cmdList->DrawIndexedInstanced(mesh->index_count, 1, 0, 0, 0);
                }
            }
        }

        // ── Splat pass (resolver path) ────────────────────────────────────────

        if (frame.splats.empty())
            return;

        const auto pipeline =
            wz::gpu::dx12::internal::get_gaussian_splat_debug_pipeline(device);

        if (!pipeline.valid())
            return;

        cmdList->SetGraphicsRootSignature(pipeline.root_sig);
        cmdList->SetPipelineState(pipeline.pso);
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

        const float vp_w = static_cast<float>(
            wz::gpu::dx12::internal::get_width(device));
        const float vp_h = static_cast<float>(
            wz::gpu::dx12::internal::get_height(device));

        for (const DrawCommand& dc : frame.splats)
        {
            if (dc.kind != DrawCommandKind::GaussianSplats)
                continue;
            if (dc.splats_buffer == INVALID_SPLAT)
                continue;

            const auto resolved = resolver.resolve_splats(dc.splats_buffer);
            if (!resolved)
                continue;

            const auto* cloud =
                wz::gpu::dx12::internal::get_gaussian_splat_cloud(
                    device, resolved->gpu_resource);
            if (!cloud || !cloud->vertex_buffer)
                continue;

            // world[16], view_proj[16], viewport_and_size[4] — matches
            // the gaussian splat debug root signature (36 x 32-bit constants).
            float constants[36] = {};
            for (int i = 0; i < 16; ++i) constants[i]      = dc.world.m[i];
            for (int i = 0; i < 16; ++i) constants[16 + i] =
                frame.view.view_projection.m[i];
            constants[32] = vp_w;
            constants[33] = vp_h;
            constants[34] = 8.0f;  // base splat size in pixels
            constants[35] = 0.0f;

            cmdList->SetGraphicsRoot32BitConstants(0, 36, constants, 0);
            cmdList->IASetVertexBuffers(0, 1, &cloud->vertex_view);
            cmdList->DrawInstanced(4, cloud->splat_count, 0, 0);
        }
    }

    void submit(wz::gpu::Device& device,
                const RenderFrameView& frame,
                const wz::engine::rendering::RenderResourceResolver& resolver,
                const wz::engine::rendering::RenderablePipelineCache& pipeline_cache)
    {
        auto* cmdList = wz::gpu::dx12::internal::get_command_list(device);

        // ── Opaque mesh pass ──────────────────────────────────────────────────

        for (const DrawCommand& dc : frame.opaque)
        {
            if (dc.kind != DrawCommandKind::Mesh)
                continue;
            if (dc.mesh == INVALID_MESH)
                continue;

            const auto resolved = resolver.resolve_mesh(dc.mesh);
            if (!resolved)
                continue;

            const auto pipeline_handle = pipeline_cache.get(resolved->program);
            const auto* pl = wz::gpu::dx12::internal::get_graphics_pipeline(
                device, pipeline_handle);
            if (!pl || !pl->valid())
                continue;

            const auto* mesh = wz::gpu::dx12::internal::get_mesh(
                device, resolved->gpu_resource);
            if (!mesh || !mesh->vertex_buffer)
                continue;

            struct { Mat4 world; Mat4 view_proj; } constants;
            constants.world     = dc.world;
            constants.view_proj = frame.view.view_projection;

            cmdList->SetGraphicsRootSignature(pl->root_sig);
            cmdList->SetPipelineState(pl->pso);
            cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmdList->SetGraphicsRoot32BitConstants(0, 32, &constants, 0);
            cmdList->IASetVertexBuffers(0, 1, &mesh->vertex_view);
            cmdList->IASetIndexBuffer(&mesh->index_view);
            cmdList->DrawIndexedInstanced(mesh->index_count, 1, 0, 0, 0);
        }

        // ── Splat pass ────────────────────────────────────────────────────────

        const float vp_w = static_cast<float>(
            wz::gpu::dx12::internal::get_width(device));
        const float vp_h = static_cast<float>(
            wz::gpu::dx12::internal::get_height(device));

        for (const DrawCommand& dc : frame.splats)
        {
            if (dc.kind != DrawCommandKind::GaussianSplats)
                continue;
            if (dc.splats_buffer == INVALID_SPLAT)
                continue;

            const auto resolved = resolver.resolve_splats(dc.splats_buffer);
            if (!resolved)
                continue;

            const auto pipeline_handle = pipeline_cache.get(resolved->program);
            const auto* pl = wz::gpu::dx12::internal::get_graphics_pipeline(
                device, pipeline_handle);
            if (!pl || !pl->valid())
                continue;

            const auto* cloud = wz::gpu::dx12::internal::get_gaussian_splat_cloud(
                device, resolved->gpu_resource);
            if (!cloud || !cloud->vertex_buffer)
                continue;

            float constants[36] = {};
            for (int i = 0; i < 16; ++i) constants[i]      = dc.world.m[i];
            for (int i = 0; i < 16; ++i) constants[16 + i] =
                frame.view.view_projection.m[i];
            constants[32] = vp_w;
            constants[33] = vp_h;
            constants[34] = 0.01f;
            constants[35] = 0.0f;

            cmdList->SetGraphicsRootSignature(pl->root_sig);
            cmdList->SetPipelineState(pl->pso);
            cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            cmdList->SetGraphicsRoot32BitConstants(0, 36, constants, 0);
            cmdList->IASetVertexBuffers(0, 1, &cloud->vertex_view);
            cmdList->DrawInstanced(4, cloud->splat_count, 0, 0);
        }
    }

    void submit(wz::gpu::Device& device,
                const RenderFrameView& frame,
                const wz::engine::rendering::RenderResourceResolver& resolver,
                const wz::engine::rendering::RenderablePipelineCache& pipeline_cache,
                const wz::engine::rendering::RenderProgramPipelineCache& render_program_cache)
    {
        auto* cmdList = wz::gpu::dx12::internal::get_command_list(device);

        const float vp_w = static_cast<float>(
            wz::gpu::dx12::internal::get_width(device));
        const float vp_h = static_cast<float>(
            wz::gpu::dx12::internal::get_height(device));

        auto resolve_pipeline = [&](
            const wz::engine::rendering::ResolvedRenderableResource& resolved)
            -> const wz::gpu::dx12::internal::DX12GraphicsPipeline*
        {
            wz::gpu::GPUHandle pipeline_handle;
            if (resolved.render_program.valid())
                pipeline_handle = render_program_cache.get(resolved.render_program);
            else
                pipeline_handle = pipeline_cache.get(resolved.program);

            return wz::gpu::dx12::internal::get_graphics_pipeline(
                device, pipeline_handle);
        };

        // ── Opaque mesh pass ──────────────────────────────────────────────────

        for (const DrawCommand& dc : frame.opaque)
        {
            if (dc.kind != DrawCommandKind::Mesh) continue;
            if (dc.mesh == INVALID_MESH)          continue;

            const auto resolved = resolver.resolve_mesh(dc.mesh);
            if (!resolved) continue;

            const auto* pl = resolve_pipeline(*resolved);
            if (!pl || !pl->valid()) continue;

            const auto* mesh = wz::gpu::dx12::internal::get_mesh(
                device, resolved->gpu_resource);
            if (!mesh || !mesh->vertex_buffer) continue;

            struct { Mat4 world; Mat4 view_proj; } constants;
            constants.world     = dc.world;
            constants.view_proj = frame.view.view_projection;

            cmdList->SetGraphicsRootSignature(pl->root_sig);
            cmdList->SetPipelineState(pl->pso);
            cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmdList->SetGraphicsRoot32BitConstants(0, 32, &constants, 0);
            cmdList->IASetVertexBuffers(0, 1, &mesh->vertex_view);
            cmdList->IASetIndexBuffer(&mesh->index_view);
            cmdList->DrawIndexedInstanced(mesh->index_count, 1, 0, 0, 0);
        }

        // ── Splat pass ────────────────────────────────────────────────────────

        for (const DrawCommand& dc : frame.splats)
        {
            if (dc.kind != DrawCommandKind::GaussianSplats) continue;
            if (dc.splats_buffer == INVALID_SPLAT)          continue;

            const auto resolved = resolver.resolve_splats(dc.splats_buffer);
            if (!resolved) continue;

            const auto* pl = resolve_pipeline(*resolved);
            if (!pl || !pl->valid()) continue;

            const auto* cloud = wz::gpu::dx12::internal::get_gaussian_splat_cloud(
                device, resolved->gpu_resource);
            if (!cloud) continue;

            // Constants buffer sized to fit both the legacy 36-dword PullDebug
            // layout and the extended 48-dword NeighborhoodColorBlend layout.
            // The actual count pushed is driven by
            // `layout->root_constants[i].value_count`, so the old program
            // reads only [0..35] and never sees the LOD slots even though
            // they are populated below.
            float constants[48] = {};
            for (int i = 0; i < 16; ++i) constants[i]      = dc.world.m[i];
            for (int i = 0; i < 16; ++i) constants[16 + i] =
                frame.view.view_projection.m[i];
            constants[32] = vp_w;
            constants[33] = vp_h;
            constants[34] = 0.01f;
            constants[35] = 0.0f;

            // LOD slots (consumed by NeighborhoodColorBlend; ignored by
            // PullDebug). [36..39] = mode, strength, near, far.
            // [40..43] = stride_ratio, max_stride, use_confidence, pad.
            // [44..47] reserved/pad.
            {
                const auto& lod = wz::gpu::dx12::internal::get_lod_settings(device);
                constants[36] = static_cast<float>(static_cast<uint32_t>(lod.mode));
                constants[37] = lod.strength;
                constants[38] = lod.near_distance;
                constants[39] = lod.far_distance;

                const uint32_t total = cloud->splat_count;
                const uint32_t rendered =
                    dc.splat_instance_count > 0
                        ? dc.splat_instance_count
                        : total;
                const float stride_ratio = (total > 0)
                    ? static_cast<float>(rendered) / static_cast<float>(total)
                    : 1.0f;
                constants[40] = stride_ratio;
                constants[41] = lod.max_stride_for_blend;
                constants[42] = lod.use_confidence ? 1.0f : 0.0f;
                constants[43] = 0.0f;
            }

            // Determine binding model.  Default to SplatVertexInstanced when no
            // render-program handle is attached.  If a valid handle is present but
            // absent from the cache, the pipeline was never realized — skip rather
            // than silently misrouting to a wrong binding path.
            wz::engine::assets::RenderBindingModel binding_model =
                wz::engine::assets::RenderBindingModel::SplatVertexInstanced;

            if (resolved->render_program.valid())
            {
                const auto maybe = render_program_cache.get_binding_model(
                    resolved->render_program);
                if (!maybe.has_value())
                    continue;   // valid handle, but pipeline was never realized
                binding_model = *maybe;
            }

            if (binding_model == wz::engine::assets::RenderBindingModel::SplatPull)
            {
                if (!cloud->valid_for_splat_pull()) continue;

                auto* srv_heap =
                    wz::gpu::dx12::internal::get_srv_cbv_uav_heap(device);
                if (!srv_heap) continue;

                const auto* layout = render_program_cache.get_binding_layout(
                    resolved->render_program);
                if (!layout || !layout->valid()) continue;

                // Upload externally-computed sorted indices if provided.
                // Empty span = keep existing t1 buffer (identity or prior sort).
                if (!dc.sorted_splat_indices.empty())
                    wz::gpu::dx12::internal::update_sorted_indices(
                        *cloud, dc.sorted_splat_indices);

                // Use the visible instance count from the DrawCommand when
                // available (back-to-front sorted subset); fall back to the
                // full cloud count for identity/legacy draws.
                const uint32_t instance_count =
                    dc.splat_instance_count > 0
                        ? dc.splat_instance_count
                        : cloud->splat_count;

                cmdList->SetDescriptorHeaps(1, &srv_heap);
                cmdList->SetGraphicsRootSignature(pl->root_sig);
                cmdList->SetPipelineState(pl->pso);
                cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

                // Root constants — iterate declared bindings; no hardcoded index.
                for (const auto& rc : layout->root_constants)
                    cmdList->SetGraphicsRoot32BitConstants(
                        rc.root_parameter_index, rc.value_count, constants, 0);

                // Descriptor tables — one SetGraphicsRootDescriptorTable per
                // visibility group; heap_start_slot offsets into srv_table.
                for (const auto& dt : layout->desc_tables)
                    cmdList->SetGraphicsRootDescriptorTable(
                        dt.root_parameter_index,
                        cloud->srv_table.gpu_at(dt.heap_start_slot));

                cmdList->DrawInstanced(4, instance_count, 0, 0);
            }
            else
            {
                if (!cloud->valid_for_vertex_instanced()) continue;

                cmdList->SetGraphicsRootSignature(pl->root_sig);
                cmdList->SetPipelineState(pl->pso);
                cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
                cmdList->SetGraphicsRoot32BitConstants(0, 36, constants, 0);
                cmdList->IASetVertexBuffers(0, 1, &cloud->vertex_view);
                cmdList->DrawInstanced(4, cloud->splat_count, 0, 0);
            }
        }
    }

    void destroy(Context* ctx)
    {
        if (!ctx) return;

        OutputDebugStringA("dx12::destroy called\n");

        if (ctx->vertex_buffer)
        {
            OutputDebugStringA("  releasing vertex_buffer\n");
            ctx->vertex_buffer->Release();
            ctx->vertex_buffer = nullptr;
        }

        if (ctx->pso)
        {
            char buf[128];
            sprintf_s(buf, "  releasing pso: %p\n", ctx->pso);
            OutputDebugStringA(buf);
            ctx->pso->Release();
            ctx->pso = nullptr;

        }

        if (ctx->root_sig)
        {
            OutputDebugStringA("  releasing root_sig\n");
            ctx->root_sig->Release();
            ctx->root_sig = nullptr;
        }

        delete ctx;
    }
}