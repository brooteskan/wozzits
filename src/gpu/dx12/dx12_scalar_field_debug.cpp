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
        D3D12_ROOT_PARAMETER params[2]{};

        // param 0: SRV table
        D3D12_DESCRIPTOR_RANGE srv_range{};
        srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv_range.NumDescriptors = 1;
        srv_range.BaseShaderRegister = 0;
        srv_range.RegisterSpace = 0;
        srv_range.OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable.NumDescriptorRanges = 1;
        params[0].DescriptorTable.pDescriptorRanges = &srv_range;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // param 1: debug constants
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[1].Constants.Num32BitValues = 4;
        params[1].Constants.ShaderRegister = 0; // b0
        params[1].Constants.RegisterSpace = 0;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

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
        desc.NumParameters = 2;
        desc.pParameters = params;
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
        ctx->display_min = desc.display_min;
        ctx->display_max = desc.display_max;
        ctx->normalize_for_display = desc.normalize_for_display;

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
        assert(impl->cmd);
        assert(impl->scalar_debug_ctx);
        assert(impl->scalar_debug_ctx->root_sig);
        assert(impl->scalar_debug_ctx->pso);
        assert(impl->scalar_field_srv_heap);

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

        impl->cmd->SetPipelineState(
            impl->scalar_debug_ctx->pso
        );

        impl->cmd->SetGraphicsRootDescriptorTable(
            0,          // root parameter 0: SRV table t0
            tex->srv_gpu
        );

        struct DebugParams
        {
            float display_min;
            float display_max;
            float inv_range;
            uint32_t flags;
        };

        static_assert(sizeof(DebugParams) == 16);

        DebugParams params{};
        params.display_min = impl->scalar_debug_ctx->display_min;
        params.display_max = impl->scalar_debug_ctx->display_max;

        const float range = params.display_max - params.display_min;

        params.inv_range =
            (range > 0.0f)
            ? (1.0f / range)
            : 0.0f;

        params.flags =
            impl->scalar_debug_ctx->normalize_for_display ? 1u : 0u;

        impl->cmd->SetGraphicsRoot32BitConstants(
            1,          // root parameter 1: debug constants b0
            4,          // display_min, display_max, inv_range, flags
            &params,
            0
        );

        impl->cmd->IASetPrimitiveTopology(
            D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
        );

        impl->cmd->DrawInstanced(3, 1, 0, 0);
    }
}