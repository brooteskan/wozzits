// src/gpu/dx12/dx12_mesh_wireframe_debug.cpp

#include <gpu/dx12/dx12.h>

#include "dx12_device_internal.h"

#include <gpu/dx12/dx12_internal.h>

#include <cassert>
#include <cstdint>

namespace wz::gpu::dx12
{
    namespace
    {
        ID3D12PipelineState* create_mesh_wireframe_pso(
            Device& device,
            ID3D12RootSignature* root_sig,
            GPUHandle vertex_shader,
            GPUHandle pixel_shader)
        {
            const DX12Shader* vs =
                internal::get_shader(device, vertex_shader);

            const DX12Shader* ps =
                internal::get_shader(device, pixel_shader);

            assert(vs);
            assert(ps);
            assert(vs->blob);
            assert(ps->blob);
            assert(vs->stage == ShaderStage::Vertex);
            assert(ps->stage == ShaderStage::Pixel);

            D3D12_INPUT_ELEMENT_DESC layout[] =
            {
                {
                    "POSITION",
                    0,
                    DXGI_FORMAT_R32G32B32_FLOAT,
                    0,
                    0,
                    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                    0
                }
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
            desc.pRootSignature = root_sig;

            desc.VS = {
                vs->blob->GetBufferPointer(),
                vs->blob->GetBufferSize()
            };

            desc.PS = {
                ps->blob->GetBufferPointer(),
                ps->blob->GetBufferSize()
            };

            desc.InputLayout = {
                layout,
                static_cast<UINT>(std::size(layout))
            };

            desc.PrimitiveTopologyType =
                D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

            desc.RasterizerState =
                CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

            // This is the whole point of the diagnostic path.
            desc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
            desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

            desc.BlendState =
                CD3DX12_BLEND_DESC(D3D12_DEFAULT);

            desc.DepthStencilState =
                CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            desc.DepthStencilState.DepthEnable = FALSE;
            desc.DSVFormat = DXGI_FORMAT_UNKNOWN;

            desc.NumRenderTargets = 1;
            desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

            desc.SampleMask = UINT_MAX;
            desc.SampleDesc.Count = 1;

            ID3D12PipelineState* pso = nullptr;

            HRESULT hr = internal::get_device(device)->CreateGraphicsPipelineState(
                &desc,
                IID_PPV_ARGS(&pso)
            );

            if (FAILED(hr))
                return nullptr;

            return pso;
        }
    }

    void create_mesh_wireframe_debug_context(
        Device& device,
        const MeshWireframeDebugContextDesc& desc)
    {
        assert(desc.valid());

        auto* impl = static_cast<DX12Device*>(device.impl);
        assert(impl);
        assert(!impl->mesh_wire_debug_ctx);

        auto* ctx = new MeshWireframeDebugContext{};
        ctx->mesh = desc.mesh;

        ctx->root_sig =
            internal::create_empty_root_signature(impl->device);

        assert(ctx->root_sig);

        ctx->pso = create_mesh_wireframe_pso(
            device,
            ctx->root_sig,
            desc.vertex_shader,
            desc.pixel_shader
        );

        assert(ctx->pso);

        impl->mesh_wire_debug_ctx = ctx;
    }

    void submit_mesh_wireframe_debug_frame(
        Device& device,
        const MeshWireframeDebugView& view)
    {
        auto* impl = static_cast<DX12Device*>(device.impl);
        assert(impl);
        assert(impl->mesh_wire_debug_ctx);
        assert(impl->cmd);

        MeshWireframeDebugContext* ctx = impl->mesh_wire_debug_ctx;

        const internal::DX12MeshResource* mesh =
            internal::get_mesh(device, ctx->mesh);

        assert(mesh);
        assert(mesh->vertex_buffer);
        assert(mesh->index_buffer);
        assert(mesh->vertex_count > 0);
        assert(mesh->index_count > 0);

        ID3D12GraphicsCommandList* cmd = impl->cmd;

        cmd->SetGraphicsRootSignature(ctx->root_sig);
        cmd->SetPipelineState(ctx->pso);

        // Root parameter 0: 32 x 32-bit constants.
        // Layout: world[16], view_proj[16].
        cmd->SetGraphicsRoot32BitConstants(
            0,
            16,
            view.world,
            0
        );

        cmd->SetGraphicsRoot32BitConstants(
            0,
            16,
            view.view_proj,
            16
        );

        cmd->IASetPrimitiveTopology(
            D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
        );

        cmd->IASetVertexBuffers(
            0,
            1,
            &mesh->vertex_view
        );

        cmd->IASetIndexBuffer(
            &mesh->index_view
        );

        cmd->DrawIndexedInstanced(
            mesh->index_count,
            1,
            0,
            0,
            0
        );
    }
}