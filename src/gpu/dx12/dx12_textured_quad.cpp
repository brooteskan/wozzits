// src/gpu/dx12/dx12_textured_quad.cpp
//
// Textured 3D quad: draw a unit quad transformed by a caller-supplied MVP,
// sampling an arbitrary RGBA texture. The S6 "3D-mesh surface" consumer -- render
// the puppet into an offscreen texture, then display that texture on a world-space
// mesh face that carries its own transform (a spinning card / cube face). General
// and reusable: any texture on any transformed quad.
//
// Same lazy root-sig / PSO / 1-descriptor SRV-heap shape as the fullscreen blit
// (dx12_blit.cpp), with two differences: the vertex shader emits a real 6-vertex
// quad (two triangles) instead of a fullscreen triangle, and a 16-float column-major
// MVP arrives as a root 32-bit-constant block (b0) that transforms each corner.
// Depth is disabled (a single quad never self-occludes); the quad composites over
// whatever colour target is bound, so in the scene it reads as a card floating in
// front. Occlusion against scene depth is a later refinement.

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
    constexpr DXGI_FORMAT kQuadTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    // A unit quad in the z=0 plane (local corners at +/-1), transformed by the
    // caller MVP. UVs follow the D3D convention (v=0 at the top): local (-1,-1) is
    // the bottom-left corner and samples texel (0,1). Two triangles, 6 vertices.
    constexpr char kQuadShader[] =
        "cbuffer QuadConstants : register(b0) { column_major float4x4 gMVP; };\n"
        "Texture2D    gTex : register(t0);\n"
        "SamplerState gSmp : register(s0);\n"
        "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
        "static const float2 kPos[6] = {\n"
        "    float2(-1,-1), float2( 1,-1), float2( 1, 1),\n"
        "    float2(-1,-1), float2( 1, 1), float2(-1, 1) };\n"
        "static const float2 kUV[6] = {\n"
        "    float2(0,1), float2(1,1), float2(1,0),\n"
        "    float2(0,1), float2(1,0), float2(0,0) };\n"
        "VSOut vs_main(uint vid : SV_VertexID) {\n"
        "    VSOut o;\n"
        "    o.pos = mul(gMVP, float4(kPos[vid], 0.0, 1.0));\n"
        "    o.uv  = kUV[vid];\n"
        "    return o;\n"
        "}\n"
        "float4 ps_main(VSOut i) : SV_Target {\n"
        "    return gTex.Sample(gSmp, i.uv);\n"
        "}\n";

    ID3DBlob* compile_quad(const char* entry, const char* target)
    {
        ID3DBlob* blob = nullptr;
        ID3DBlob* err = nullptr;
        HRESULT hr = D3DCompile(
            kQuadShader, sizeof(kQuadShader) - 1, "textured_quad", nullptr,
            nullptr, entry, target, 0, 0, &blob, &err);
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

    ID3D12RootSignature* create_quad_root_signature(ID3D12Device* device)
    {
        D3D12_DESCRIPTOR_RANGE srv_range{};
        srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv_range.NumDescriptors = 1;
        srv_range.BaseShaderRegister = 0;  // t0
        srv_range.OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[2]{};
        // [0] source-texture SRV table (t0), sampled in the pixel shader.
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable.NumDescriptorRanges = 1;
        params[0].DescriptorTable.pDescriptorRanges = &srv_range;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        // [1] MVP root constants (b0, 16 floats), consumed in the vertex shader.
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[1].Constants.ShaderRegister = 0;  // b0
        params[1].Constants.RegisterSpace = 0;
        params[1].Constants.Num32BitValues = 16;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

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
        desc.NumParameters = 2;
        desc.pParameters = params;
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

    ID3D12PipelineState* create_quad_pso(
        ID3D12Device* device, ID3D12RootSignature* rs,
        ID3DBlob* vs, ID3DBlob* ps)
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = rs;
        desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
        desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
        desc.InputLayout = { nullptr, 0 };  // quad emitted from SV_VertexID
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        // Two-sided: a spinning quad shows its back face for half the turn.
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.DepthStencilState.StencilEnable = FALSE;
        desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = kQuadTargetFormat;
        desc.SampleMask = UINT_MAX;
        desc.SampleDesc.Count = 1;

        ID3D12PipelineState* pso = nullptr;
        HRESULT hr = device->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&pso));
        return FAILED(hr) ? nullptr : pso;
    }

    // Lazily build the quad root sig / PSO / 1-descriptor shader-visible SRV heap.
    bool ensure_quad_ctx(wz::gpu::dx12::DX12Device* impl)
    {
        if (impl->textured_quad_ctx) {
            return impl->textured_quad_ctx->root_sig
                && impl->textured_quad_ctx->pso
                && impl->textured_quad_ctx->srv_heap;
        }
        auto* ctx = new wz::gpu::dx12::TexturedQuadContext{};
        ID3DBlob* vs = compile_quad("vs_main", "vs_5_0");
        ID3DBlob* ps = compile_quad("ps_main", "ps_5_0");
        if (vs && ps) {
            ctx->root_sig = create_quad_root_signature(impl->device);
            if (ctx->root_sig) {
                ctx->pso = create_quad_pso(impl->device, ctx->root_sig, vs, ps);
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

        impl->textured_quad_ctx = ctx;
        return ctx->root_sig && ctx->pso && ctx->srv_heap;
    }
}

namespace wz::gpu::dx12::internal
{
    // Draw `texture` on a unit quad transformed by `mvp` (16 floats, column-major:
    // the same layout the engine feeds view_projection to its shaders). Must be
    // inside a begin_frame/end_frame bracket with a colour target bound; the texture
    // must rest shader-readable. Depth is disabled, so the quad draws over whatever
    // is already in the target.
    bool draw_textured_quad_dx12(
        Device& device, GPUHandle texture, const float mvp[16])
    {
        auto* impl = static_cast<DX12Device*>(device.impl);
        if (!impl || !impl->cmd || !impl->device || !mvp) {
            return false;
        }
        DX12Texture* tex = impl->textures.get(texture);
        if (!tex || !tex->valid()) {
            return false;
        }
        if (!ensure_quad_ctx(impl)) {
            return false;
        }
        TexturedQuadContext* ctx = impl->textured_quad_ctx;

        // (Re)write the source texture's SRV into the quad heap's single slot.
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
        impl->cmd->SetGraphicsRoot32BitConstants(1, 16, mvp, 0);
        impl->cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        impl->cmd->DrawInstanced(6, 1, 0, 0);
        return true;
    }
}
