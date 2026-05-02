// file: src/engine/render_backends/dx12/dx12_submit.cpp

#include <d3d12.h>

#include <engine/render_backends/dx12/dx12_submit.h>
#include <gpu/dx12/dx12_internal.h>

#include <iostream>

namespace wz::render::backend::dx12
{


    Context* create(wz::gpu::Device& device)
    {
        ID3D12Device* dev = wz::gpu::dx12::internal::get_device(device);
        // auto* dev = wz::gpu::dx12::internal::get_device(device);
        Context* ctx = new Context();
        ctx->device = &device;

        ctx->root_sig = wz::gpu::dx12::internal::create_empty_root_signature(dev);
        ctx->pso = wz::gpu::dx12::internal::create_triangle_pso(dev, ctx->root_sig);
        assert(ctx->pso);

        struct Vertex
        {
            float x, y, z;
        };

        Vertex tri[3] =
        {
            {  0.0f,  0.5f, 0.0f },
            {  0.5f, -0.5f, 0.0f },
            { -0.5f, -0.5f, 0.0f }
        };

        const UINT buffer_size = sizeof(tri);


        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = buffer_size;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = dev->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&ctx->vertex_buffer)
        );
        assert(SUCCEEDED(hr));

        void* mapped = nullptr;
        ctx->vertex_buffer->Map(0, nullptr, &mapped);
        memcpy(mapped, tri, buffer_size);
        ctx->vertex_buffer->Unmap(0, nullptr);

        ctx->vb_view.BufferLocation = ctx->vertex_buffer->GetGPUVirtualAddress();
        ctx->vb_view.StrideInBytes = sizeof(Vertex);
        ctx->vb_view.SizeInBytes = buffer_size;

        return ctx;
    }

    void submit(Context* ctx, const RenderFrame& frame)
    {

        assert(ctx);
        assert(ctx->device);
        assert(ctx->device->impl);

        auto* cmdList =
            wz::gpu::dx12::internal::get_command_list(*ctx->device);

        // ────── bind invariant state ─────────────────────────────
        cmdList->SetGraphicsRootSignature(ctx->root_sig);
        cmdList->SetPipelineState(ctx->pso);

        cmdList->IASetPrimitiveTopology(
            D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
        );

        cmdList->IASetVertexBuffers(0, 1, &ctx->vb_view);

        // ────── iterate draw commands ─────────────────────────────
        for (const DrawCommand& dc : frame.commands)
        {
            if (dc.stage != PipelineStage::OpaqueGeometry)
                continue;

            cmdList->DrawInstanced(3, 1, 0, 0);
        }
    }

        void destroy(Context* ctx)
    {
        if (!ctx) return;

        if (ctx->vertex_buffer)
        {
            ctx->vertex_buffer->Release();
            ctx->vertex_buffer = nullptr;
        }

        if (ctx->pso)
        {
            ctx->pso->Release();
            ctx->pso = nullptr;
        }

        if (ctx->root_sig)
        {
            ctx->root_sig->Release();
            ctx->root_sig = nullptr;
        }

        delete ctx;
    }
}