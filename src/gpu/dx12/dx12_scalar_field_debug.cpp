// src/gpu/dx12/dx12_scalar_field_debug.cpp

#include "dx12_device_internal.h"

#include <gpu/dx12/dx12.h>
#include <gpu/dx12/dx12_internal.h>
#include <gpu/dx12/external/d3dx12.h>

#include <cassert>

namespace
{
    static constexpr DXGI_FORMAT BACKBUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

    ID3D12RootSignature* create_scalar_field_root_signature(
        ID3D12Device* device)
    {
        D3D12_DESCRIPTOR_RANGE srv_range{};
        srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv_range.NumDescriptors = 1;
        srv_range.BaseShaderRegister = 0; // t0
        srv_range.RegisterSpace = 0;
        srv_range.OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER param{};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        param.DescriptorTable.NumDescriptorRanges = 1;
        param.DescriptorTable.pDescriptorRanges = &srv_range;
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MipLODBias = 0.0f;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0; // s0
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = 1;
        desc.pParameters = &param;
        desc.NumStaticSamplers = 1;
        desc.pStaticSamplers = &sampler;
        desc.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ID3DBlob* sig_blob = nullptr;
        ID3DBlob* error_blob = nullptr;

        HRESULT hr = D3D12SerializeRootSignature(
            &desc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &sig_blob,
            &error_blob
        );

        if (FAILED(hr))
        {
            if (error_blob)
                error_blob->Release();

            return nullptr;
        }

        ID3D12RootSignature* root_sig = nullptr;

        hr = device->CreateRootSignature(
            0,
            sig_blob->GetBufferPointer(),
            sig_blob->GetBufferSize(),
            IID_PPV_ARGS(&root_sig)
        );

        sig_blob->Release();
        if (error_blob)
            error_blob->Release();

        if (FAILED(hr))
            return nullptr;

        return root_sig;
    }

    ID3D12PipelineState* create_scalar_field_pso(
        wz::gpu::Device& device,
        ID3D12RootSignature* root_sig,
        wz::gpu::GPUHandle vertex_shader,
        wz::gpu::GPUHandle pixel_shader)
    {
        const wz::gpu::dx12::DX12Shader* vs =
            wz::gpu::dx12::internal::get_shader(device, vertex_shader);

        const wz::gpu::dx12::DX12Shader* ps =
            wz::gpu::dx12::internal::get_shader(device, pixel_shader);

        if (!vs || !ps || !vs->blob || !ps->blob)
            return nullptr;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = root_sig;

        desc.VS = {
            vs->blob->GetBufferPointer(),
            vs->blob->GetBufferSize()
        };

        desc.PS = {
            ps->blob->GetBufferPointer(),
            ps->blob->GetBufferSize()
        };

        // Fullscreen triangle uses SV_VertexID, no vertex buffer.
        desc.InputLayout = { nullptr, 0 };
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.DepthStencilState.StencilEnable = FALSE;
        desc.DSVFormat = DXGI_FORMAT_UNKNOWN;

        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = BACKBUFFER_FORMAT;

        desc.SampleMask = UINT_MAX;
        desc.SampleDesc.Count = 1;

        ID3D12PipelineState* pso = nullptr;

        HRESULT hr =
            wz::gpu::dx12::internal::get_device(device)
            ->CreateGraphicsPipelineState(
                &desc,
                IID_PPV_ARGS(&pso)
            );

        if (FAILED(hr))
            return nullptr;

        return pso;
    }
}

namespace wz::gpu::dx12
{
    void create_scalar_field_debug_context(
        wz::gpu::Device& device,
        const ScalarFieldDebugContextDesc& desc)
    {
        assert(desc.valid());

        auto* impl = static_cast<DX12Device*>(device.impl);
        assert(impl);
        assert(!impl->scalar_debug_ctx);

        auto* ctx = new ScalarFieldDebugContext{};
        ctx->scalar_field_texture = desc.scalar_field_texture;

        ctx->root_sig = create_scalar_field_root_signature(impl->device);
        assert(ctx->root_sig);

        ctx->pso = create_scalar_field_pso(
            device,
            ctx->root_sig,
            desc.vertex_shader,
            desc.pixel_shader
        );
        assert(ctx->pso);

        impl->scalar_debug_ctx = ctx;
    }

    void submit_scalar_field_debug_frame(
        wz::gpu::Device& device)
    {
        auto* impl = static_cast<DX12Device*>(device.impl);
        assert(impl);
        assert(impl->scalar_debug_ctx);

        const auto* tex =
            impl->scalar_field_textures.get(
                impl->scalar_debug_ctx->scalar_field_texture
            );

        assert(tex);
        assert(tex->texture);

        ID3D12DescriptorHeap* heaps[] = {
            impl->scalar_field_srv_heap
        };

        impl->cmd->SetDescriptorHeaps(1, heaps);
        impl->cmd->SetGraphicsRootSignature(
            impl->scalar_debug_ctx->root_sig
        );
        impl->cmd->SetGraphicsRootDescriptorTable(
            0,
            tex->srv_gpu
        );

        impl->cmd->SetPipelineState(
            impl->scalar_debug_ctx->pso
        );

        impl->cmd->IASetPrimitiveTopology(
            D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
        );

        impl->cmd->DrawInstanced(3, 1, 0, 0);
    }
}