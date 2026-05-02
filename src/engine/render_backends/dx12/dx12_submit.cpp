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

        Context* ctx = new Context();
        ctx->device = &device;

        ctx->root_sig = wz::gpu::dx12::internal::create_empty_root_signature(dev);
        ctx->pso = wz::gpu::dx12::internal::create_triangle_pso(dev, ctx->root_sig);
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

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = vb_size;
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

        return ctx;
    }

    void submit(Context* ctx, const RenderFrame& frame)
    {
        assert(ctx);
        assert(ctx->device);
        assert(ctx->device->impl);

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

        for (const DrawCommand& dc : frame.commands)
        {
            if (dc.stage != PipelineStage::OpaqueGeometry)
                continue;

            if (dc.mesh >= ctx->mesh_table.size())
                continue;

            const auto& mesh = ctx->mesh_table[dc.mesh];

            cmdList->IASetVertexBuffers(0, 1, &mesh.vb_view);

            data.world = dc.world;
            data.view_proj = ctx->view_proj;

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
                cmdList->DrawInstanced(mesh.index_count, 1, 0, 0);
            }
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