// src/gpu/dx12/dx12_gaussian_splat_debug.cpp

#include <gpu/dx12/dx12.h>

#include "dx12_device_internal.h"

#include <gpu/dx12/dx12_internal.h>

#include <cassert>
#include <cstdint>

namespace wz::gpu::dx12
{
    namespace
    {
        ID3D12RootSignature* create_gaussian_splat_debug_root_signature(
            ID3D12Device* device)
        {
            D3D12_ROOT_PARAMETER param = {};
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;

            // world[16], view_proj[16], viewport_and_size[4]
            param.Constants.Num32BitValues = 36;
            param.Constants.RegisterSpace = 0;
            param.Constants.ShaderRegister = 0;
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

            D3D12_ROOT_SIGNATURE_DESC desc = {};
            desc.NumParameters = 1;
            desc.pParameters = &param;
            desc.NumStaticSamplers = 0;
            desc.pStaticSamplers = nullptr;
            desc.Flags =
                D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            ID3DBlob* sig_blob = nullptr;
            ID3DBlob* error_blob = nullptr;

            HRESULT hr = D3D12SerializeRootSignature(
                &desc,
                D3D_ROOT_SIGNATURE_VERSION_1,
                &sig_blob,
                &error_blob);

            if (FAILED(hr)) {
                if (error_blob) {
                    OutputDebugStringA(
                        static_cast<const char*>(error_blob->GetBufferPointer()));
                    error_blob->Release();
                }

                assert(false);
                return nullptr;
            }

            ID3D12RootSignature* root_sig = nullptr;

            hr = device->CreateRootSignature(
                0,
                sig_blob->GetBufferPointer(),
                sig_blob->GetBufferSize(),
                IID_PPV_ARGS(&root_sig));

            assert(SUCCEEDED(hr));

            sig_blob->Release();

            if (error_blob)
                error_blob->Release();

            return root_sig;
        }

        ID3D12PipelineState* create_gaussian_splat_debug_pso(
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
                    D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,
                    1
                },
                {
                    "SCALE",
                    0,
                    DXGI_FORMAT_R32_FLOAT,
                    0,
                    12,
                    D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,
                    1
                },
                {
                    "COLOR",
                    0,
                    DXGI_FORMAT_R32G32B32_FLOAT,
                    0,
                    16,
                    D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,
                    1
                },
                {
                    "OPACITY",
                    0,
                    DXGI_FORMAT_R32_FLOAT,
                    0,
                    28,
                    D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,
                    1
                },
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
            desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

            desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

            D3D12_RENDER_TARGET_BLEND_DESC& rt_blend =
                desc.BlendState.RenderTarget[0];

            rt_blend.BlendEnable = TRUE;
            rt_blend.LogicOpEnable = FALSE;

            rt_blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            rt_blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            rt_blend.BlendOp = D3D12_BLEND_OP_ADD;

            rt_blend.SrcBlendAlpha = D3D12_BLEND_ONE;
            rt_blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            rt_blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;

            rt_blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

            desc.DepthStencilState =
                CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            desc.DepthStencilState.DepthEnable = FALSE;
            desc.DSVFormat = DXGI_FORMAT_UNKNOWN;

            desc.NumRenderTargets = 1;
            desc.RTVFormats[0] = internal::get_backbuffer_format();

            desc.SampleMask = UINT_MAX;
            desc.SampleDesc.Count = 1;

            ID3D12PipelineState* pso = nullptr;

            HRESULT hr = internal::get_device(device)->CreateGraphicsPipelineState(
                &desc,
                IID_PPV_ARGS(&pso));

            if (FAILED(hr)) {
                char buf[256];
                sprintf_s(
                    buf,
                    "CreateGraphicsPipelineState failed for gaussian splat debug: 0x%08X\n",
                    static_cast<unsigned>(hr));
                OutputDebugStringA(buf);
                return nullptr;
            }

            return pso;
        }
    }

    void create_gaussian_splat_debug_context(
        Device& device,
        const GaussianSplatDebugContextDesc& desc)
    {
        assert(desc.valid());

        auto* impl = static_cast<DX12Device*>(device.impl);
        assert(impl);
        assert(!impl->gaussian_splat_debug_ctx);

        auto* ctx = new GaussianSplatDebugContext{};
        ctx->splat_cloud = desc.splat_cloud;

        ctx->root_sig =
            create_gaussian_splat_debug_root_signature(impl->device);

        assert(ctx->root_sig);

        ctx->pso = create_gaussian_splat_debug_pso(
            device,
            ctx->root_sig,
            desc.vertex_shader,
            desc.pixel_shader);

        assert(ctx->pso);

        impl->gaussian_splat_debug_ctx = ctx;
    }

    void submit_gaussian_splat_debug_frame(
        Device& device,
        const GaussianSplatDebugView& view)
    {
        auto* impl = static_cast<DX12Device*>(device.impl);
        assert(impl);
        assert(impl->gaussian_splat_debug_ctx);
        assert(impl->cmd);

        GaussianSplatDebugContext* ctx =
            impl->gaussian_splat_debug_ctx;

        const internal::DX12GaussianSplatCloudResource* cloud =
            internal::get_gaussian_splat_cloud(device, ctx->splat_cloud);

        assert(cloud);
        assert(cloud->vertex_buffer);
        assert(cloud->splat_count > 0);

        ID3D12GraphicsCommandList* cmd = impl->cmd;

        cmd->SetGraphicsRootSignature(ctx->root_sig);
        cmd->SetPipelineState(ctx->pso);

        // Root parameter 0:
        // world[16], view_proj[16], viewport_and_size[4].
        cmd->SetGraphicsRoot32BitConstants(
            0,
            16,
            view.world,
            0);

        cmd->SetGraphicsRoot32BitConstants(
            0,
            16,
            view.view_proj,
            16);

        cmd->SetGraphicsRoot32BitConstants(
            0,
            4,
            view.viewport_and_size,
            32);

        cmd->IASetPrimitiveTopology(
            D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

        cmd->IASetVertexBuffers(
            0,
            1,
            &cloud->vertex_view);

        cmd->DrawInstanced(
            4,
            cloud->splat_count,
            0,
            0);
    }
}