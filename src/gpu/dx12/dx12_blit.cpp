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

    // Fullscreen encode (#324): same triangle + sampler as the blit, but the
    // pixel shader applies the IEC 61966-2-1 linear->sRGB transfer so a linear
    // RGBA16F source lands display-referred in the UNORM backbuffer. The branch
    // is the exact piecewise sRGB curve (not a bare 2.2 pow), so darks match
    // hardware sRGB decode and a texel round-trips decode->encode to itself.
    constexpr char kSrgbEncodeShader[] =
        "Texture2D    gTex : register(t0);\n"
        "SamplerState gSmp : register(s0);\n"
        "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
        "float3 linear_to_srgb(float3 c) {\n"
        "    c = max(c, 0.0);\n"
        "    float3 lo = c * 12.92;\n"
        "    float3 hi = 1.055 * pow(c, 1.0 / 2.4) - 0.055;\n"
        "    float3 use_lo = step(c, 0.0031308);\n"
        "    return lerp(hi, lo, use_lo);\n"
        "}\n"
        "float4 ps_main(VSOut i) : SV_Target {\n"
        "    float4 c = gTex.Sample(gSmp, i.uv);\n"
        "    return float4(linear_to_srgb(c.rgb), c.a);\n"
        "}\n";

    ID3DBlob* compile_blit(const char* src, size_t src_len, const char* name,
                           const char* entry, const char* target)
    {
        ID3DBlob* blob = nullptr;
        ID3DBlob* err = nullptr;
        HRESULT hr = D3DCompile(
            src, src_len, name, nullptr, nullptr,
            entry, target, D3DCOMPILE_ENABLE_STRICTNESS, 0, &blob, &err);
        // The blob was released BEFORE the FAILED() test, so a compile failure
        // returned nullptr with no diagnostic anywhere -- the message naming the
        // broken line was destroyed one statement before anyone could ask
        // whether it existed (issue #316, C3-C2).
        if (err) {
            OutputDebugStringA("dx12_blit: HLSL diagnostics -- ");
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
        ID3DBlob* vs = compile_blit(
            kBlitShader, sizeof(kBlitShader) - 1, "blit", "vs_main", "vs_5_0");
        ID3DBlob* ps = compile_blit(
            kBlitShader, sizeof(kBlitShader) - 1, "blit", "ps_main", "ps_5_0");
        // The #324 encode reuses the fullscreen VS + root sig; only its pixel
        // shader differs, so it is a second PSO on this context (the same
        // one-root-sig-many-PSOs shape TexturedQuadContext already uses).
        ID3DBlob* encode_ps = compile_blit(
            kSrgbEncodeShader, sizeof(kSrgbEncodeShader) - 1, "srgb_encode",
            "ps_main", "ps_5_0");
        if (vs && ps) {
            ctx->root_sig = create_blit_root_signature(impl->device);
            if (ctx->root_sig) {
                ctx->pso = create_blit_pso(impl->device, ctx->root_sig, vs, ps);
                if (encode_ps) {
                    ctx->pso_srgb_encode = create_blit_pso(
                        impl->device, ctx->root_sig, vs, encode_ps);
                }
            }
        }
        if (vs) {
            vs->Release();
        }
        if (ps) {
            ps->Release();
        }
        if (encode_ps) {
            encode_ps->Release();
        }
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        // kFramesInFlight partitions of kSrvCapacity each: slot s uses
        // [s*cap, (s+1)*cap) so an in-flight frame never overwrites another
        // slot's descriptors (#306); begin_frame sets the cursor to the base.
        hd.NumDescriptors = wz::gpu::dx12::BlitContext::kSrvCapacity
            * wz::gpu::dx12::DX12Device::kFramesInFlight;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        impl->device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&ctx->srv_heap));
        ctx->srv_stride = impl->device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // Start the cursor in the CURRENT slot's partition, not at 0. This context
        // is built lazily, on the first blit of whichever frame first needs one --
        // which may be any slot. The struct default of 0 plus the per-frame guard
        // (which only checks the partition's upper bound) meant a context first
        // created while frame_slot == 1 wrote descriptors 0..cap-1, i.e. slot 0's
        // partition, while slot 0's frame could still be in flight reading them.
        // begin_frame rebases the cursor from here on, so this only had to be wrong
        // once, on the frame that created the context.
        ctx->srv_cursor =
            impl->frame_slot * wz::gpu::dx12::BlitContext::kSrvCapacity;

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

        // Write the source texture's SRV into the NEXT ring slot. The GPU reads
        // the slot at execute time, so a slot is never rewritten within a frame
        // (see BlitContext); the cursor resets to this slot's base in begin_frame.
        if (ctx->srv_cursor
            >= (impl->frame_slot + 1u) * BlitContext::kSrvCapacity) {
            return false;  // out of blit slots this frame; refuse, don't alias
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

        D3D12_GPU_DESCRIPTOR_HANDLE slot_gpu =
            ctx->srv_heap->GetGPUDescriptorHandleForHeapStart();
        slot_gpu.ptr += static_cast<UINT64>(slot) * ctx->srv_stride;

        ID3D12DescriptorHeap* heaps[] = { ctx->srv_heap };
        impl->cmd->SetDescriptorHeaps(1, heaps);
        impl->cmd->SetGraphicsRootSignature(ctx->root_sig);
        impl->cmd->SetPipelineState(ctx->pso);
        impl->cmd->SetGraphicsRootDescriptorTable(0, slot_gpu);
        impl->cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        impl->cmd->DrawInstanced(3, 1, 0, 0);
        return true;
    }

    // Fullscreen linear->sRGB encode of `linear_texture` onto the currently-bound
    // backbuffer (#324). Identical plumbing to blit_texture_dx12 -- shared root
    // sig, SRV ring slot, fullscreen triangle -- differing only in the PSO, whose
    // pixel shader applies the sRGB transfer. Must be inside a begin/end_frame
    // bracket with the (UNORM) backbuffer bound; `linear_texture` must rest
    // shader-readable (end_primary_color_pass leaves the scene target so).
    bool encode_srgb_to_backbuffer_dx12(Device& device, GPUHandle linear_texture)
    {
        auto* impl = static_cast<DX12Device*>(device.impl);
        if (!impl || !impl->cmd || !impl->device) {
            return false;
        }
        DX12Texture* tex = impl->textures.get(linear_texture);
        if (!tex || !tex->valid()) {
            return false;
        }
        if (!ensure_blit_ctx(impl) || !impl->blit_ctx->pso_srgb_encode) {
            return false;
        }
        BlitContext* ctx = impl->blit_ctx;

        // Same per-frame SRV ring discipline as the blit: never rewrite a slot
        // within a frame (the GPU reads it at execute time); cursor resets to
        // this frame slot's base in begin_frame.
        if (ctx->srv_cursor
            >= (impl->frame_slot + 1u) * BlitContext::kSrvCapacity) {
            return false;
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

        D3D12_GPU_DESCRIPTOR_HANDLE slot_gpu =
            ctx->srv_heap->GetGPUDescriptorHandleForHeapStart();
        slot_gpu.ptr += static_cast<UINT64>(slot) * ctx->srv_stride;

        ID3D12DescriptorHeap* heaps[] = { ctx->srv_heap };
        impl->cmd->SetDescriptorHeaps(1, heaps);
        impl->cmd->SetGraphicsRootSignature(ctx->root_sig);
        impl->cmd->SetPipelineState(ctx->pso_srgb_encode);
        impl->cmd->SetGraphicsRootDescriptorTable(0, slot_gpu);
        impl->cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        impl->cmd->DrawInstanced(3, 1, 0, 0);
        return true;
    }
}
