// src/gpu/dx12/dx12_blit.cpp
//
// Fullscreen-triangle blit: sample an arbitrary RGBA texture onto the current
// render target. The S6 "2D surface" consumer -- render the puppet into an
// offscreen texture, then display that texture on a screen-space quad. General and
// reusable (compositing, post-fx). Mirrors the scalar-field debug blit's structure
// (fullscreen triangle via SV_VertexID + an SRV table + static sampler) but with a
// straight RGBA sample and no debug constants. Lazily built + reused per device.

#include "dx12_device_internal.h"

#include <gpu/dx12/dx12.h>
#include <gpu/dx12/dx12_internal.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wswitch"
#include <gpu/dx12/external/d3dx12.h>
#pragma clang diagnostic pop

#include <d3dcompiler.h>

namespace
{
    constexpr DXGI_FORMAT kBlitTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    // Fullscreen triangle (generated from SV_VertexID, no vertex buffer) + a
    // straight texture sample.
    constexpr char kBlitShader[] =
        "Texture2D    gTex : register(t0);\n"
        "SamplerState gSmp : register(s0);\n"
        "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
        "VSOut vs_main(uint vid : SV_VertexID) {\n"
        "    VSOut o;\n"
        "    float2 uv = float2((vid << 1) & 2, vid & 2);\n"
        "    o.uv = uv;\n"
        "    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);\n"
        "    return o;\n"
        "}\n"
        "float4 ps_main(VSOut i) : SV_Target {\n"
        "    return gTex.Sample(gSmp, i.uv);\n"
        "}\n";

    ID3DBlob* compile_blit(const char* entry, const char* target)
    {
        ID3DBlob* blob = nullptr;
        ID3DBlob* err = nullptr;
        HRESULT hr = D3DCompile(
            kBlitShader, sizeof(kBlitShader) - 1, "blit", nullptr, nullptr,
            entry, target, 0, 0, &blob, &err);
        if (err) {
            err->Release();
        }
        if (FAILED(hr)) {
            if (blob) {
                blob->Release();
            }
            return nullptr;
        }
        return blob;
    }

    ID3D12RootSignature* create_blit_root_signature(ID3D12Device* device)
    {
        D3D12_DESCRIPTOR_RANGE srv_range{};
        srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv_range.NumDescriptors = 1;
        srv_range.BaseShaderRegister = 0;  // t0
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
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;  // s0
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = 1;
        desc.pParameters = &param;
        desc.NumStaticSamplers = 1;
        desc.pStaticSamplers = &sampler;
        desc.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ID3DBlob* sig = nullptr;
        ID3DBlob* err = nullptr;
        HRESULT hr = D3D12SerializeRootSignature(
            &desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
        if (err) {
            err->Release();
        }
        if (FAILED(hr)) {
            return nullptr;
        }
        ID3D12RootSignature* rs = nullptr;
        hr = device->CreateRootSignature(
            0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&rs));
        sig->Release();
        return FAILED(hr) ? nullptr : rs;
    }

    ID3D12PipelineState* create_blit_pso(
        ID3D12Device* device, ID3D12RootSignature* rs,
        ID3DBlob* vs, ID3DBlob* ps)
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = rs;
        desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
        desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
        desc.InputLayout = { nullptr, 0 };  // fullscreen triangle from SV_VertexID
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.DepthStencilState.StencilEnable = FALSE;
        desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = kBlitTargetFormat;
        desc.SampleMask = UINT_MAX;
        desc.SampleDesc.Count = 1;

        ID3D12PipelineState* pso = nullptr;
        HRESULT hr = device->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&pso));
        return FAILED(hr) ? nullptr : pso;
    }

    // Lazily build the blit root sig / PSO / 1-descriptor shader-visible SRV heap.
    bool ensure_blit_ctx(wz::gpu::dx12::DX12Device* impl)
    {
        if (impl->blit_ctx) {
            return impl->blit_ctx->root_sig && impl->blit_ctx->pso
                && impl->blit_ctx->srv_heap;
        }
        auto* ctx = new wz::gpu::dx12::BlitContext{};
        ID3DBlob* vs = compile_blit("vs_main", "vs_5_0");
        ID3DBlob* ps = compile_blit("ps_main", "ps_5_0");
        if (vs && ps) {
            ctx->root_sig = create_blit_root_signature(impl->device);
            if (ctx->root_sig) {
                ctx->pso = create_blit_pso(impl->device, ctx->root_sig, vs, ps);
            }
        }
        if (vs) {
            vs->Release();
        }
        if (ps) {
            ps->Release();
        }
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = 1;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        impl->device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&ctx->srv_heap));

        impl->blit_ctx = ctx;
        return ctx->root_sig && ctx->pso && ctx->srv_heap;
    }
}

namespace wz::gpu::dx12::internal
{
    // Draw `texture` onto the currently-bound render target as a fullscreen quad.
    // Must be inside a begin_frame/end_frame bracket with a color target already
    // bound (the caller's render pass); the texture must rest shader-readable.
    bool blit_texture_dx12(Device& device, GPUHandle texture)
    {
        auto* impl = static_cast<DX12Device*>(device.impl);
        if (!impl || !impl->cmd || !impl->device) {
            return false;
        }
        DX12Texture* tex = impl->textures.get(texture);
        if (!tex || !tex->valid()) {
            return false;
        }
        if (!ensure_blit_ctx(impl)) {
            return false;
        }
        BlitContext* ctx = impl->blit_ctx;

        // (Re)write the source texture's SRV into the blit heap's single slot.
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = tex->format;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MostDetailedMip = 0;
        srv.Texture2D.MipLevels = 1;
        impl->device->CreateShaderResourceView(
            tex->texture, &srv,
            ctx->srv_heap->GetCPUDescriptorHandleForHeapStart());

        ID3D12DescriptorHeap* heaps[] = { ctx->srv_heap };
        impl->cmd->SetDescriptorHeaps(1, heaps);
        impl->cmd->SetGraphicsRootSignature(ctx->root_sig);
        impl->cmd->SetPipelineState(ctx->pso);
        impl->cmd->SetGraphicsRootDescriptorTable(
            0, ctx->srv_heap->GetGPUDescriptorHandleForHeapStart());
        impl->cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        impl->cmd->DrawInstanced(3, 1, 0, 0);
        return true;
    }
}
