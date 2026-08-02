// src/gpu/dx12/dx12_textured_quad.cpp
//
// Textured 3D quad + layered texture compositing.
//
// draw_textured_quad_dx12 draws a unit quad transformed by a caller-supplied
// column-major MVP, sampling an arbitrary RGBA texture, tinted by a constant.
// Three modes share one root signature (see TexturedQuadMode):
//   Overlay      -- opaque, depth off        (the S6 2D-surface consumer)
//   WorldSurface -- premult alpha + depth test, no write (an in-scene surface)
//   Composite    -- premult alpha, depth off (drawing INTO a texture)
//
// composite_texture_layers_dx12 builds on that: clear a target render-target
// texture to a base colour, then draw N textured layers into it, each placed by
// centre/half-extent in the TARGET's UV space with an optional rotation and
// tint. That is the general "material compositing" operation -- layer art onto a
// material texture that a mesh then samples (decals, labels, layered materials,
// a puppet on a sphere). Nothing here knows about inochi.
//
// The quad VS emits a real 6-vertex quad (two triangles) from SV_VertexID -- no
// vertex buffer -- and transforms it by the MVP root constants.

#include "dx12_device_internal.h"

#include <gpu/dx12/dx12.h>
#include <gpu/dx12/dx12_internal.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wswitch"
#include <gpu/dx12/external/d3dx12.h>
#pragma clang diagnostic pop

#include <d3dcompiler.h>

#include <cmath>

namespace
{
    constexpr DXGI_FORMAT kQuadTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    // A unit quad in the z=0 plane (local corners at +/-1), transformed by the
    // caller MVP. UVs follow the D3D convention (v=0 at the top): local (-1,-1) is
    // the bottom-left corner and samples texel (0,1). Two triangles, 6 vertices.
    // The sampled colour is multiplied by `tint` (premultiplied-friendly: tint the
    // colour AND the alpha so a faded layer stays premultiplied).
    constexpr char kQuadShader[] =
        "cbuffer QuadConstants : register(b0) {\n"
        "    column_major float4x4 gMVP;\n"
        "    float4 gTint;\n"
        "};\n"
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
        "    return gTex.Sample(gSmp, i.uv) * gTint;\n"
        "}\n";

    // 16 MVP floats + 4 tint floats.
    constexpr UINT kQuadRootConstantCount = 20;

    ID3DBlob* compile_quad(const char* entry, const char* target)
    {
        ID3DBlob* blob = nullptr;
        ID3DBlob* err = nullptr;
        HRESULT hr = D3DCompile(
            kQuadShader, sizeof(kQuadShader) - 1, "textured_quad", nullptr,
            nullptr, entry, target, 0, 0, &blob, &err);
        // Released before the FAILED() test, so a compile failure produced no
        // diagnostic at all -- see dx12_blit.cpp (issue #316, C3-C2).
        if (err) {
            OutputDebugStringA("dx12_textured_quad: HLSL diagnostics -- ");
            OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
            OutputDebugStringA("\n");
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
        // [1] MVP + tint root constants (b0). Visible to both stages: the VS reads
        // the matrix, the PS reads the tint.
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[1].Constants.ShaderRegister = 0;  // b0
        params[1].Constants.RegisterSpace = 0;
        params[1].Constants.Num32BitValues = kQuadRootConstantCount;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

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

    // alpha_blend: premultiplied-alpha composite (ONE / INV_SRC_ALPHA). Every
    // texture this path draws is premultiplied -- content rendered with
    // SRC_ALPHA/INV_SRC_ALPHA into a black-cleared target -- so ONE is the correct
    // (fringe-free) source factor.
    // depth_test: LESS_EQUAL, no write, against the shared D32 depth. Matches the
    // engine's convention (depth cleared to 1.0 = far), so nearer scene geometry
    // occludes the quad while the quad writes no depth.
    ID3D12PipelineState* create_quad_pso(
        ID3D12Device* device, ID3D12RootSignature* rs,
        ID3DBlob* vs, ID3DBlob* ps, bool alpha_blend, bool depth_test)
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
        desc.DepthStencilState.StencilEnable = FALSE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = kQuadTargetFormat;
        desc.SampleMask = UINT_MAX;
        desc.SampleDesc.Count = 1;

        if (alpha_blend) {
            D3D12_RENDER_TARGET_BLEND_DESC& rt = desc.BlendState.RenderTarget[0];
            rt.BlendEnable = TRUE;
            rt.SrcBlend = D3D12_BLEND_ONE;           // premultiplied-alpha source
            rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            rt.BlendOp = D3D12_BLEND_OP_ADD;
            rt.SrcBlendAlpha = D3D12_BLEND_ONE;
            rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }

        if (depth_test) {
            desc.DepthStencilState.DepthEnable = TRUE;
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        }
        else {
            desc.DepthStencilState.DepthEnable = FALSE;
            desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
        }

        ID3D12PipelineState* pso = nullptr;
        HRESULT hr = device->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&pso));
        return FAILED(hr) ? nullptr : pso;
    }

    // Lazily build the quad root sig / the three PSOs / a 1-descriptor
    // shader-visible SRV heap.
    bool ensure_quad_ctx(wz::gpu::dx12::DX12Device* impl)
    {
        if (impl->textured_quad_ctx) {
            const auto* c = impl->textured_quad_ctx;
            return c->root_sig && c->pso && c->pso_world && c->pso_composite
                && c->srv_heap;
        }
        auto* ctx = new wz::gpu::dx12::TexturedQuadContext{};
        ID3DBlob* vs = compile_quad("vs_main", "vs_5_0");
        ID3DBlob* ps = compile_quad("ps_main", "ps_5_0");
        if (vs && ps) {
            ctx->root_sig = create_quad_root_signature(impl->device);
            if (ctx->root_sig) {
                ctx->pso = create_quad_pso(
                    impl->device, ctx->root_sig, vs, ps, false, false);
                ctx->pso_world = create_quad_pso(
                    impl->device, ctx->root_sig, vs, ps, true, true);
                ctx->pso_composite = create_quad_pso(
                    impl->device, ctx->root_sig, vs, ps, true, false);
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
        hd.NumDescriptors = wz::gpu::dx12::TexturedQuadContext::kSrvCapacity;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        impl->device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&ctx->srv_heap));
        ctx->srv_stride = impl->device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        impl->textured_quad_ctx = ctx;
        return ctx->root_sig && ctx->pso && ctx->pso_world && ctx->pso_composite
            && ctx->srv_heap;
    }
}

namespace wz::gpu::dx12::internal
{
    bool draw_textured_quad_dx12(
        Device& device, GPUHandle texture, const float mvp[16],
        TexturedQuadMode mode, const float tint_rgba[4])
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

        // Write the source texture's SRV into the NEXT ring slot. Descriptors
        // are read at execute time, so a slot is never rewritten within a frame
        // — the old single rewritten slot made every recorded quad draw sample
        // the LAST texture written. Cursor resets in begin_frame.
        if (ctx->srv_cursor >= TexturedQuadContext::kSrvCapacity) {
            return false;  // out of quad slots this frame; refuse, don't alias
        }
        const uint32_t slot = ctx->srv_cursor++;

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = tex->format;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MostDetailedMip = 0;
        srv.Texture2D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE slot_cpu =
            ctx->srv_heap->GetCPUDescriptorHandleForHeapStart();
        slot_cpu.ptr += static_cast<SIZE_T>(slot) * ctx->srv_stride;
        impl->device->CreateShaderResourceView(tex->texture, &srv, slot_cpu);

        ID3D12PipelineState* pso = ctx->pso;
        if (mode == TexturedQuadMode::WorldSurface) {
            pso = ctx->pso_world;
        }
        else if (mode == TexturedQuadMode::Composite) {
            pso = ctx->pso_composite;
        }

        // Root constants: 16 MVP floats then 4 tint floats (default opaque white).
        float constants[kQuadRootConstantCount];
        for (int i = 0; i < 16; ++i) {
            constants[i] = mvp[i];
        }
        for (int i = 0; i < 4; ++i) {
            constants[16 + i] = tint_rgba ? tint_rgba[i] : 1.0f;
        }

        D3D12_GPU_DESCRIPTOR_HANDLE slot_gpu =
            ctx->srv_heap->GetGPUDescriptorHandleForHeapStart();
        slot_gpu.ptr += static_cast<UINT64>(slot) * ctx->srv_stride;

        ID3D12DescriptorHeap* heaps[] = { ctx->srv_heap };
        impl->cmd->SetDescriptorHeaps(1, heaps);
        impl->cmd->SetGraphicsRootSignature(ctx->root_sig);
        impl->cmd->SetPipelineState(pso);
        impl->cmd->SetGraphicsRootDescriptorTable(0, slot_gpu);
        impl->cmd->SetGraphicsRoot32BitConstants(
            1, kQuadRootConstantCount, constants, 0);
        impl->cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        impl->cmd->DrawInstanced(6, 1, 0, 0);
        return true;
    }

    bool composite_texture_layers_dx12(
        Device& device,
        GPUHandle target,
        const float base_color[4],
        const TextureCompositeLayer* layers,
        std::size_t layer_count)
    {
        auto* impl = static_cast<DX12Device*>(device.impl);
        if (!impl || !impl->cmd) {
            return false;
        }
        if (layer_count > 0 && !layers) {
            return false;
        }

        // Base layer = the clear. Everything else composites over it in order.
        if (!begin_offscreen_pass(device, target, base_color)) {
            return false;
        }

        bool ok = true;
        for (std::size_t i = 0; i < layer_count; ++i) {
            const TextureCompositeLayer& layer = layers[i];
            if (!layer.texture.valid()) {
                continue;
            }

            // Place the unit quad into the TARGET's UV space: centre + half-extent
            // in [0,1] UV, mapped to NDC (x: 2u-1, y: 1-2v -- v runs down), with an
            // optional rotation about the layer centre. Non-uniform half-extents
            // are honoured (the rotation then shears, as any placement transform
            // does).
            const float cx = layer.center_uv[0] * 2.0f - 1.0f;
            const float cy = 1.0f - layer.center_uv[1] * 2.0f;
            const float sx = layer.half_size_uv[0] * 2.0f;
            const float sy = layer.half_size_uv[1] * 2.0f;
            const float cos_r = std::cos(layer.rotation);
            const float sin_r = std::sin(layer.rotation);

            const float mvp[16] = {
                 sx * cos_r, sy * sin_r, 0.0f, 0.0f,   // column 0
                -sx * sin_r, sy * cos_r, 0.0f, 0.0f,   // column 1
                 0.0f,       0.0f,       0.0f, 0.0f,   // column 2 (quad z = 0)
                 cx,         cy,         0.5f, 1.0f,   // column 3
            };
            // Premultiplied tint: scale colour AND alpha so a faded layer stays
            // premultiplied and composites correctly.
            const float tint[4] = {
                layer.opacity, layer.opacity, layer.opacity, layer.opacity };

            if (!draw_textured_quad_dx12(
                    device, layer.texture, mvp,
                    TexturedQuadMode::Composite, tint)) {
                ok = false;
            }
        }

        if (!end_offscreen_pass(device, target)) {
            return false;
        }
        return ok;
    }
}
