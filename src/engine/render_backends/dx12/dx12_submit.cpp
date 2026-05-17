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

    void submit(Context* ctx,
                const RenderFrameView& frame,
                const wz::engine::rendering::RenderResourceResolver& resolver)
    {
        assert(ctx);
        assert(ctx->device);

        auto* cmdList =
            wz::gpu::dx12::internal::get_command_list(*ctx->device);

        // ── Opaque pass (mesh table, same as non-resolver path) ───────────────

        cmdList->SetGraphicsRootSignature(ctx->root_sig);
        cmdList->SetPipelineState(ctx->pso);
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        struct { Mat4 world; Mat4 view_proj; } data;

        for (const DrawCommand& dc : frame.opaque)
        {
            if (dc.mesh >= ctx->mesh_table.size())
                continue;

            const auto& mesh = ctx->mesh_table[dc.mesh];
            cmdList->IASetVertexBuffers(0, 1, &mesh.vb_view);

            data.world     = dc.world;
            data.view_proj = frame.view.view_projection;
            cmdList->SetGraphicsRoot32BitConstants(0, 32, &data, 0);

            if (mesh.index_buffer)
                cmdList->DrawIndexedInstanced(mesh.index_count, 1, 0, 0, 0);
            else
                cmdList->DrawInstanced(mesh.index_count, 1, 0, 0);
        }

        // ── Splat pass (resolver path) ────────────────────────────────────────

        if (frame.splats.empty())
            return;

        const auto pipeline =
            wz::gpu::dx12::internal::get_gaussian_splat_debug_pipeline(
                *ctx->device);

        if (!pipeline.valid())
            return;

        cmdList->SetGraphicsRootSignature(pipeline.root_sig);
        cmdList->SetPipelineState(pipeline.pso);
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

        const float vp_w = static_cast<float>(
            wz::gpu::dx12::internal::get_width(*ctx->device));
        const float vp_h = static_cast<float>(
            wz::gpu::dx12::internal::get_height(*ctx->device));

        for (const DrawCommand& dc : frame.splats)
        {
            if (dc.kind != DrawCommandKind::GaussianSplats)
                continue;
            if (dc.splats_buffer == INVALID_SPLAT)
                continue;

            const wz::gpu::GPUHandle gpu =
                resolver.resolve_splats(dc.splats_buffer);
            if (!gpu.valid())
                continue;

            const auto* cloud =
                wz::gpu::dx12::internal::get_gaussian_splat_cloud(
                    *ctx->device, gpu);
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