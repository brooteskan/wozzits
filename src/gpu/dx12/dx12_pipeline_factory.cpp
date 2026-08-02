// src/gpu/dx12/dx12_pipeline_factory.cpp
//
// Single source of truth for built-in DX12 root signature and PSO creation.
// The legacy debug context files delegate to these functions during the
// transition to RenderablePipelineCache.

#include <gpu/dx12/dx12_pipeline_factory.h>
#include <gpu/dx12/dx12_internal.h>

#include <engine/assets/mesh/mesh.h>

#include "dx12_device_internal.h"

#include <cassert>
#include <vector>

namespace wz::gpu::dx12::internal
{
    // ── Blend state (THE single translation; see dx12_internal.h) ─────────────

    // Adding a member to wz::rhi::BlendMode -- which lives in the SEPARATE
    // wozzits-rhi repo -- must fail this build rather than silently render
    // opaque. The switch below has no default:, and this pins the enum's
    // extent so an appended member is caught even though the build is /W3
    // with no /WX. If you are reading this because it fired: add the case
    // below and bump the expected ordinal.
    static_assert(
        static_cast<int>(wz::rhi::BlendMode::SliceFromDestination) == 7,
        "wz::rhi::BlendMode gained or lost a member -- add its case to "
        "render_target_blend_desc() before updating this assert. Both PSO "
        "factories go through that function; an unhandled mode falls back to "
        "D3D12_DEFAULT, which is BlendEnable FALSE, i.e. fully opaque.");

    D3D12_RENDER_TARGET_BLEND_DESC
    render_target_blend_desc(wz::rhi::BlendMode mode) noexcept
    {
        D3D12_RENDER_TARGET_BLEND_DESC rt =
            CD3DX12_BLEND_DESC(D3D12_DEFAULT).RenderTarget[0];
        rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        const auto set = [&rt](D3D12_BLEND src,
                               D3D12_BLEND dst,
                               D3D12_BLEND_OP op,
                               D3D12_BLEND src_a,
                               D3D12_BLEND dst_a,
                               D3D12_BLEND_OP op_a) noexcept {
            rt.BlendEnable    = TRUE;
            rt.LogicOpEnable  = FALSE;
            rt.SrcBlend       = src;
            rt.DestBlend      = dst;
            rt.BlendOp        = op;
            rt.SrcBlendAlpha  = src_a;
            rt.DestBlendAlpha = dst_a;
            rt.BlendOpAlpha   = op_a;
        };

        switch (mode) {
        case wz::rhi::BlendMode::Opaque:
            // D3D12_DEFAULT already is BlendEnable FALSE.
            break;
        case wz::rhi::BlendMode::AlphaBlend:
            set(D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA,
                D3D12_BLEND_OP_ADD,
                D3D12_BLEND_ONE, D3D12_BLEND_INV_SRC_ALPHA,
                D3D12_BLEND_OP_ADD);
            break;
        case wz::rhi::BlendMode::Additive:
            // Glow accumulation: src*ONE + dst*ONE. The star PS pre-weights its
            // colour by the sprite falloff, so ONE/ONE simply sums light onto
            // the sky (overlapping stars brighten, nothing occludes).
            set(D3D12_BLEND_ONE, D3D12_BLEND_ONE, D3D12_BLEND_OP_ADD,
                D3D12_BLEND_ONE, D3D12_BLEND_ONE, D3D12_BLEND_OP_ADD);
            break;
        case wz::rhi::BlendMode::Multiply:
            // src*DstColor + dst*InvSrcAlpha. Darkens the destination by the
            // source -- exactly Src x Dst for an OPAQUE source. The InvSrcAlpha
            // dest factor is what makes it usable on a partially-covering
            // source: a transparent premultiplied texel has rgb 0, so a ZERO
            // dest factor would multiply the destination to BLACK there instead
            // of leaving it alone. Alpha accumulates coverage as a normal
            // "over", so a puppet rendered into an RTT still ends with
            // alpha = coverage whatever its Parts' modes.
            set(D3D12_BLEND_DEST_COLOR, D3D12_BLEND_INV_SRC_ALPHA,
                D3D12_BLEND_OP_ADD,
                D3D12_BLEND_ONE, D3D12_BLEND_INV_SRC_ALPHA,
                D3D12_BLEND_OP_ADD);
            break;
        case wz::rhi::BlendMode::Screen:
            // src*ONE + dst*InvSrcColor = Src + Dst - Src x Dst. The complement
            // of Multiply; lightens (highlight / glow compositing).
            set(D3D12_BLEND_ONE, D3D12_BLEND_INV_SRC_COLOR, D3D12_BLEND_OP_ADD,
                D3D12_BLEND_ONE, D3D12_BLEND_INV_SRC_ALPHA,
                D3D12_BLEND_OP_ADD);
            break;
        case wz::rhi::BlendMode::PremultipliedAlpha:
            // src*ONE + dst*InvSrcAlpha. "Over" for a PS that already
            // multiplied rgb by alpha, so a texel at alpha 0.5 no longer
            // contributes only half its colour on top of a full-weight
            // AlphaBlend fade -- which is what darkened transparent Part
            // borders (#277). Identical to AlphaBlend in the alpha channel.
            set(D3D12_BLEND_ONE, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD,
                D3D12_BLEND_ONE, D3D12_BLEND_INV_SRC_ALPHA,
                D3D12_BLEND_OP_ADD);
            break;
        case wz::rhi::BlendMode::SourceAtop:
            // src*DstAlpha + dst*InvSrcAlpha. Porter-Duff "source atop": the
            // source shows only where the destination is already covered, so it
            // paints ONTO what is beneath without extending it. Inochi's
            // ClipToLower (#299). Unlike Multiply/Screen, alpha uses the SAME
            // factors as colour rather than accumulating coverage -- both
            // scaled by DstAlpha keeps the result premultiplied-consistent, and
            // alpha growing where colour was clipped to zero would leave a dark
            // fringe once the target is composited.
            set(D3D12_BLEND_DEST_ALPHA, D3D12_BLEND_INV_SRC_ALPHA,
                D3D12_BLEND_OP_ADD,
                D3D12_BLEND_DEST_ALPHA, D3D12_BLEND_INV_SRC_ALPHA,
                D3D12_BLEND_OP_ADD);
            break;
        case wz::rhi::BlendMode::SliceFromDestination:
            // src*InvDstAlpha - dst*InvSrcAlpha. The cutting counterpart of
            // SourceAtop: where the source covers, the destination is removed.
            // Inochi's SliceFromLower (#299).
            //
            // OPERATOR DIRECTION IS THE TRAP. The GL reference uses
            // FUNC_SUBTRACT, which is src-dst. D3D12's _SUBTRACT is dst-src;
            // src-dst is _REV_SUBTRACT. Getting it backwards inverts the
            // operator into something that still renders, just wrongly.
            set(D3D12_BLEND_INV_DEST_ALPHA, D3D12_BLEND_INV_SRC_ALPHA,
                D3D12_BLEND_OP_REV_SUBTRACT,
                D3D12_BLEND_INV_DEST_ALPHA, D3D12_BLEND_INV_SRC_ALPHA,
                D3D12_BLEND_OP_REV_SUBTRACT);
            break;
        }
        return rt;
    }

    // ── Root signatures ───────────────────────────────────────────────────────

    static ID3D12RootSignature* create_mesh_wireframe_root_sig(ID3D12Device* device)
    {
        // 32 × 32-bit constants (world[16] + view_proj[16]), register 0, VS only.
        D3D12_ROOT_PARAMETER param = {};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        param.Constants.Num32BitValues = 40;
        param.Constants.RegisterSpace  = 0;
        param.Constants.ShaderRegister = 0;
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters   = 1;
        desc.pParameters     = &param;
        desc.NumStaticSamplers = 0;
        desc.pStaticSamplers = nullptr;
        desc.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ID3DBlob* sig_blob   = nullptr;
        ID3DBlob* error_blob = nullptr;

        HRESULT hr = D3D12SerializeRootSignature(
            &desc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &sig_blob,
            &error_blob);

        if (FAILED(hr))
        {
            if (error_blob)
            {
                OutputDebugStringA(
                    static_cast<const char*>(error_blob->GetBufferPointer()));
                error_blob->Release();
            }
            return nullptr;
        }

        ID3D12RootSignature* root_sig = nullptr;
        hr = device->CreateRootSignature(
            0,
            sig_blob->GetBufferPointer(),
            sig_blob->GetBufferSize(),
            IID_PPV_ARGS(&root_sig));

        sig_blob->Release();
        if (error_blob) error_blob->Release();

        assert(SUCCEEDED(hr));
        return root_sig;
    }

    static ID3D12RootSignature* create_mesh_field_heatmap_root_sig(
        ID3D12Device* device)
    {
        D3D12_DESCRIPTOR_RANGE field_range{};
        field_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        field_range.NumDescriptors = 1;
        field_range.BaseShaderRegister = 0;
        field_range.RegisterSpace = 0;
        field_range.OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.Num32BitValues = 40;
        params[0].Constants.RegisterSpace  = 0;
        params[0].Constants.ShaderRegister = 0;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[1].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &field_range;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters   = 2;
        desc.pParameters     = params;
        desc.NumStaticSamplers = 0;
        desc.pStaticSamplers = nullptr;
        desc.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ID3DBlob* sig_blob   = nullptr;
        ID3DBlob* error_blob = nullptr;

        HRESULT hr = D3D12SerializeRootSignature(
            &desc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &sig_blob,
            &error_blob);

        if (FAILED(hr))
        {
            if (error_blob)
            {
                OutputDebugStringA(
                    static_cast<const char*>(error_blob->GetBufferPointer()));
                error_blob->Release();
            }
            return nullptr;
        }

        ID3D12RootSignature* root_sig = nullptr;
        hr = device->CreateRootSignature(
            0,
            sig_blob->GetBufferPointer(),
            sig_blob->GetBufferSize(),
            IID_PPV_ARGS(&root_sig));

        sig_blob->Release();
        if (error_blob) error_blob->Release();

        assert(SUCCEEDED(hr));
        return root_sig;
    }

    static ID3D12RootSignature* create_mesh_mask_style_root_sig(
        ID3D12Device* device)
    {
        D3D12_DESCRIPTOR_RANGE field_range{};
        field_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        field_range.NumDescriptors = 1;
        field_range.BaseShaderRegister = 0;
        field_range.RegisterSpace = 0;
        field_range.OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE rules_range{};
        rules_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        rules_range.NumDescriptors = 1;
        rules_range.BaseShaderRegister = 1;
        rules_range.RegisterSpace = 0;
        rules_range.OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[3]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.Num32BitValues = 48;
        params[0].Constants.RegisterSpace  = 0;
        params[0].Constants.ShaderRegister = 0;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[1].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &field_range;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[2].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].DescriptorTable.NumDescriptorRanges = 1;
        params[2].DescriptorTable.pDescriptorRanges = &rules_range;
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters   = 3;
        desc.pParameters     = params;
        desc.NumStaticSamplers = 0;
        desc.pStaticSamplers = nullptr;
        desc.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ID3DBlob* sig_blob   = nullptr;
        ID3DBlob* error_blob = nullptr;

        HRESULT hr = D3D12SerializeRootSignature(
            &desc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &sig_blob,
            &error_blob);

        if (FAILED(hr))
        {
            if (error_blob)
            {
                OutputDebugStringA(
                    static_cast<const char*>(error_blob->GetBufferPointer()));
                error_blob->Release();
            }
            return nullptr;
        }

        ID3D12RootSignature* root_sig = nullptr;
        hr = device->CreateRootSignature(
            0,
            sig_blob->GetBufferPointer(),
            sig_blob->GetBufferSize(),
            IID_PPV_ARGS(&root_sig));

        sig_blob->Release();
        if (error_blob) error_blob->Release();

        assert(SUCCEEDED(hr));
        return root_sig;
    }

    static ID3D12RootSignature* create_gaussian_splat_root_sig(ID3D12Device* device)
    {
        // 36 × 32-bit constants:
        //   world[16], view_proj[16], viewport_and_size[4]
        D3D12_ROOT_PARAMETER param = {};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        param.Constants.Num32BitValues = 36;
        param.Constants.RegisterSpace  = 0;
        param.Constants.ShaderRegister = 0;
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters   = 1;
        desc.pParameters     = &param;
        desc.NumStaticSamplers = 0;
        desc.pStaticSamplers = nullptr;
        desc.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ID3DBlob* sig_blob   = nullptr;
        ID3DBlob* error_blob = nullptr;

        HRESULT hr = D3D12SerializeRootSignature(
            &desc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &sig_blob,
            &error_blob);

        if (FAILED(hr))
        {
            if (error_blob)
            {
                OutputDebugStringA(
                    static_cast<const char*>(error_blob->GetBufferPointer()));
                error_blob->Release();
            }
            return nullptr;
        }

        ID3D12RootSignature* root_sig = nullptr;
        hr = device->CreateRootSignature(
            0,
            sig_blob->GetBufferPointer(),
            sig_blob->GetBufferSize(),
            IID_PPV_ARGS(&root_sig));

        sig_blob->Release();
        if (error_blob) error_blob->Release();

        assert(SUCCEEDED(hr));
        return root_sig;
    }

    static ID3D12RootSignature* create_terrain_mesh_surface_root_sig(
        ID3D12Device* device)
    {
        // 48 x 32-bit constants:
        //   world[16], view_proj[16], light_position[4],
        //   light_direction[4], light_color_intensity[4], lighting_params[4].
        D3D12_ROOT_PARAMETER param = {};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        param.Constants.Num32BitValues = 48;
        param.Constants.RegisterSpace  = 0;
        param.Constants.ShaderRegister = 0;
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters   = 1;
        desc.pParameters     = &param;
        desc.NumStaticSamplers = 0;
        desc.pStaticSamplers = nullptr;
        desc.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ID3DBlob* sig_blob   = nullptr;
        ID3DBlob* error_blob = nullptr;

        HRESULT hr = D3D12SerializeRootSignature(
            &desc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &sig_blob,
            &error_blob);

        if (FAILED(hr))
        {
            if (error_blob)
            {
                OutputDebugStringA(
                    static_cast<const char*>(error_blob->GetBufferPointer()));
                error_blob->Release();
            }
            return nullptr;
        }

        ID3D12RootSignature* root_sig = nullptr;
        hr = device->CreateRootSignature(
            0,
            sig_blob->GetBufferPointer(),
            sig_blob->GetBufferSize(),
            IID_PPV_ARGS(&root_sig));

        sig_blob->Release();
        if (error_blob) error_blob->Release();

        assert(SUCCEEDED(hr));
        return root_sig;
    }

    static ID3D12RootSignature* create_sky_surface_root_sig(
        ID3D12Device* device)
    {
        // Seven float4 groups: solid color/exposure, gradient top/bottom,
        // mode/rotation, rotation/right, up, and forward.
        D3D12_DESCRIPTOR_RANGE scalar_field_range{};
        scalar_field_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        scalar_field_range.NumDescriptors = 1;
        scalar_field_range.BaseShaderRegister = 0;
        scalar_field_range.RegisterSpace = 0;
        scalar_field_range.OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.Num32BitValues = 28;
        params[0].Constants.RegisterSpace  = 0;
        params[0].Constants.ShaderRegister = 0;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[1].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &scalar_field_range;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MipLODBias = 0.0f;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters   = 2;
        desc.pParameters     = params;
        desc.NumStaticSamplers = 1;
        desc.pStaticSamplers = &sampler;
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ID3DBlob* sig_blob   = nullptr;
        ID3DBlob* error_blob = nullptr;

        HRESULT hr = D3D12SerializeRootSignature(
            &desc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &sig_blob,
            &error_blob);

        if (FAILED(hr))
        {
            if (error_blob)
            {
                OutputDebugStringA(
                    static_cast<const char*>(error_blob->GetBufferPointer()));
                error_blob->Release();
            }
            return nullptr;
        }

        ID3D12RootSignature* root_sig = nullptr;
        hr = device->CreateRootSignature(
            0,
            sig_blob->GetBufferPointer(),
            sig_blob->GetBufferSize(),
            IID_PPV_ARGS(&root_sig));

        sig_blob->Release();
        if (error_blob) error_blob->Release();

        assert(SUCCEEDED(hr));
        return root_sig;
    }

    ID3D12RootSignature* create_root_signature_for_program(
        ID3D12Device* device,
        wz::engine::assets::BuiltinRenderProgram program)
    {
        using P = wz::engine::assets::BuiltinRenderProgram;
        switch (program)
        {
        case P::MeshWireframeDebug:
        case P::MeshWireframeDepthDebug:
        case P::MeshDepthPrepassDebug:
        case P::MeshWireframeAlpha:
        case P::MeshSurface:
        case P::MeshSurfaceAlpha:
            return create_mesh_wireframe_root_sig(device);
        case P::MeshFieldHeatmap:
            return create_mesh_field_heatmap_root_sig(device);
        case P::MeshMaskStyle:
            return create_mesh_mask_style_root_sig(device);
        case P::TerrainMeshSurface:
            return create_terrain_mesh_surface_root_sig(device);
        case P::GaussianSplatDebug:
        case P::TerrainSurfelSurface:
            return create_gaussian_splat_root_sig(device);
        case P::SkySurface:
            return create_sky_surface_root_sig(device);
        default:
            return nullptr;
        }
    }

    // ── PSOs ──────────────────────────────────────────────────────────────────

    static ID3D12PipelineState* create_mesh_wireframe_pso(
        Device& device,
        ID3D12RootSignature* root_sig,
        GPUHandle vertex_shader,
        GPUHandle pixel_shader,
        bool depth_enabled,
        bool alpha_blend = false)
    {
        const DX12Shader* vs = get_shader(device, vertex_shader);
        const DX12Shader* ps = get_shader(device, pixel_shader);

        assert(vs && vs->blob);
        assert(ps && ps->blob);

        D3D12_INPUT_ELEMENT_DESC layout[] =
        {{
            "POSITION", 0,
            DXGI_FORMAT_R32G32B32_FLOAT,
            0, 0,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        }};

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature   = root_sig;
        desc.VS               = { vs->blob->GetBufferPointer(), vs->blob->GetBufferSize() };
        desc.PS               = { ps->blob->GetBufferPointer(), ps->blob->GetBufferSize() };
        desc.InputLayout      = { layout, static_cast<UINT>(std::size(layout)) };
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        desc.RasterizerState  = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        if (depth_enabled)
        {
            desc.RasterizerState.DepthBias = -64;
            desc.RasterizerState.SlopeScaledDepthBias = -1.0f;
            desc.RasterizerState.DepthBiasClamp = 0.0f;
        }

        desc.BlendState        = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        if (alpha_blend) {
            D3D12_RENDER_TARGET_BLEND_DESC& rt = desc.BlendState.RenderTarget[0];
            rt.BlendEnable           = TRUE;
            rt.LogicOpEnable         = FALSE;
            rt.SrcBlend              = D3D12_BLEND_SRC_ALPHA;
            rt.DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
            rt.BlendOp               = D3D12_BLEND_OP_ADD;
            rt.SrcBlendAlpha         = D3D12_BLEND_ONE;
            rt.DestBlendAlpha        = D3D12_BLEND_INV_SRC_ALPHA;
            rt.BlendOpAlpha          = D3D12_BLEND_OP_ADD;
            rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthEnable =
            depth_enabled ? TRUE : FALSE;
        desc.DepthStencilState.DepthWriteMask =
            D3D12_DEPTH_WRITE_MASK_ZERO;
        if (depth_enabled)
            desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        desc.DSVFormat =
            depth_enabled ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_UNKNOWN;

        desc.NumRenderTargets  = 1;
        desc.RTVFormats[0]     = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleMask        = UINT_MAX;
        desc.SampleDesc.Count  = 1;

        ID3D12PipelineState* pso = nullptr;
        HRESULT hr = get_device(device)->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&pso));

        if (FAILED(hr)) return nullptr;
        return pso;
    }

    static ID3D12PipelineState* create_mesh_depth_prepass_pso(
        Device& device,
        ID3D12RootSignature* root_sig,
        GPUHandle vertex_shader,
        GPUHandle pixel_shader)
    {
        const DX12Shader* vs = get_shader(device, vertex_shader);
        const DX12Shader* ps = get_shader(device, pixel_shader);

        assert(vs && vs->blob);
        assert(ps && ps->blob);

        D3D12_INPUT_ELEMENT_DESC layout[] =
        {{
            "POSITION", 0,
            DXGI_FORMAT_R32G32B32_FLOAT,
            0, 0,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        }};

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature   = root_sig;
        desc.VS               = { vs->blob->GetBufferPointer(), vs->blob->GetBufferSize() };
        desc.PS               = { ps->blob->GetBufferPointer(), ps->blob->GetBufferSize() };
        desc.InputLayout      = { layout, static_cast<UINT>(std::size(layout)) };
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        desc.RasterizerState  = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthEnable = TRUE;
        desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

        desc.NumRenderTargets  = 1;
        desc.RTVFormats[0]     = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleMask        = UINT_MAX;
        desc.SampleDesc.Count  = 1;

        ID3D12PipelineState* pso = nullptr;
        HRESULT hr = get_device(device)->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&pso));

        if (FAILED(hr))
            return nullptr;
        return pso;
    }

    static ID3D12PipelineState* create_terrain_mesh_surface_pso(
        Device& device,
        ID3D12RootSignature* root_sig,
        GPUHandle vertex_shader,
        GPUHandle pixel_shader,
        bool double_sided,
        bool alpha_blend = false)
    {
        const DX12Shader* vs = get_shader(device, vertex_shader);
        const DX12Shader* ps = get_shader(device, pixel_shader);

        assert(vs && vs->blob);
        assert(ps && ps->blob);

        using Vertex = wz::engine::assets::MeshVertex;

        D3D12_INPUT_ELEMENT_DESC layout[] = {
            {
                "POSITION", 0,
                DXGI_FORMAT_R32G32B32_FLOAT,
                0, static_cast<UINT>(offsetof(Vertex, position)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
            },
            {
                "NORMAL", 0,
                DXGI_FORMAT_R32G32B32_FLOAT,
                0, static_cast<UINT>(offsetof(Vertex, normal)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
            },
            {
                "TEXCOORD", 0,
                DXGI_FORMAT_R32G32_FLOAT,
                0, static_cast<UINT>(offsetof(Vertex, uv)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
            },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = root_sig;
        desc.VS = { vs->blob->GetBufferPointer(), vs->blob->GetBufferSize() };
        desc.PS = { ps->blob->GetBufferPointer(), ps->blob->GetBufferSize() };
        desc.InputLayout = { layout, static_cast<UINT>(std::size(layout)) };
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode =
            double_sided ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;

        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        if (alpha_blend) {
            D3D12_RENDER_TARGET_BLEND_DESC& rt = desc.BlendState.RenderTarget[0];
            rt.BlendEnable           = TRUE;
            rt.LogicOpEnable         = FALSE;
            rt.SrcBlend              = D3D12_BLEND_SRC_ALPHA;
            rt.DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
            rt.BlendOp               = D3D12_BLEND_OP_ADD;
            rt.SrcBlendAlpha         = D3D12_BLEND_ONE;
            rt.DestBlendAlpha        = D3D12_BLEND_INV_SRC_ALPHA;
            rt.BlendOpAlpha          = D3D12_BLEND_OP_ADD;
            rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthEnable = TRUE;
        desc.DepthStencilState.DepthWriteMask =
            alpha_blend ? D3D12_DEPTH_WRITE_MASK_ZERO : D3D12_DEPTH_WRITE_MASK_ALL;
        desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] =
            alpha_blend ? get_backbuffer_format() : DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleMask = UINT_MAX;
        desc.SampleDesc.Count = 1;

        ID3D12PipelineState* pso = nullptr;
        HRESULT hr = get_device(device)->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&pso));

        if (FAILED(hr))
            return nullptr;
        return pso;
    }

    static ID3D12PipelineState* create_gaussian_splat_pso(
        Device& device,
        ID3D12RootSignature* root_sig,
        GPUHandle vertex_shader,
        GPUHandle pixel_shader)
    {
        const DX12Shader* vs = get_shader(device, vertex_shader);
        const DX12Shader* ps = get_shader(device, pixel_shader);

        assert(vs && vs->blob);
        assert(ps && ps->blob);

        // Use explicit offsetof to account for pad0/pad1 in DX12GaussianSplatVertex.
        // D3D12_APPEND_ALIGNED_ELEMENT would compute wrong offsets for ROTATION and
        // COLOR because it doesn't know about the 4-byte padding after scale.
        D3D12_INPUT_ELEMENT_DESC layout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,
              static_cast<UINT>(offsetof(DX12GaussianSplatVertex, position)),
              D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "OPACITY",  0, DXGI_FORMAT_R32_FLOAT,           0,
              static_cast<UINT>(offsetof(DX12GaussianSplatVertex, opacity)),
              D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "SCALE",    0, DXGI_FORMAT_R32G32B32_FLOAT,     0,
              static_cast<UINT>(offsetof(DX12GaussianSplatVertex, scale)),
              D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "ROTATION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,  0,
              static_cast<UINT>(offsetof(DX12GaussianSplatVertex, rotation)),
              D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT,     0,
              static_cast<UINT>(offsetof(DX12GaussianSplatVertex, color)),
              D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature   = root_sig;
        desc.VS               = { vs->blob->GetBufferPointer(), vs->blob->GetBufferSize() };
        desc.PS               = { ps->blob->GetBufferPointer(), ps->blob->GetBufferSize() };
        desc.InputLayout      = { layout, static_cast<UINT>(std::size(layout)) };
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        desc.RasterizerState  = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        D3D12_RENDER_TARGET_BLEND_DESC& rt = desc.BlendState.RenderTarget[0];
        rt.BlendEnable          = TRUE;
        rt.LogicOpEnable        = FALSE;
        rt.SrcBlend             = D3D12_BLEND_SRC_ALPHA;
        rt.DestBlend            = D3D12_BLEND_INV_SRC_ALPHA;
        rt.BlendOp              = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha        = D3D12_BLEND_ONE;
        rt.DestBlendAlpha       = D3D12_BLEND_INV_SRC_ALPHA;
        rt.BlendOpAlpha         = D3D12_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.DSVFormat         = DXGI_FORMAT_UNKNOWN;

        desc.NumRenderTargets  = 1;
        desc.RTVFormats[0]     = get_backbuffer_format();
        desc.SampleMask        = UINT_MAX;
        desc.SampleDesc.Count  = 1;

        ID3D12PipelineState* pso = nullptr;
        HRESULT hr = get_device(device)->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&pso));

        if (FAILED(hr))
        {
            char buf[256];
            sprintf_s(buf,
                "create_pso_for_program(GaussianSplatDebug) failed: 0x%08X\n",
                static_cast<unsigned>(hr));
            OutputDebugStringA(buf);
            return nullptr;
        }
        return pso;
    }

    static ID3D12PipelineState* create_terrain_surfel_pso(
        Device& device,
        ID3D12RootSignature* root_sig,
        GPUHandle vertex_shader,
        GPUHandle pixel_shader)
    {
        const DX12Shader* vs = get_shader(device, vertex_shader);
        const DX12Shader* ps = get_shader(device, pixel_shader);

        assert(vs && vs->blob);
        assert(ps && ps->blob);

        // Same vertex layout as GaussianSplatDebug.
        D3D12_INPUT_ELEMENT_DESC layout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,
              static_cast<UINT>(offsetof(DX12GaussianSplatVertex, position)),
              D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "OPACITY",  0, DXGI_FORMAT_R32_FLOAT,           0,
              static_cast<UINT>(offsetof(DX12GaussianSplatVertex, opacity)),
              D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "SCALE",    0, DXGI_FORMAT_R32G32B32_FLOAT,     0,
              static_cast<UINT>(offsetof(DX12GaussianSplatVertex, scale)),
              D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "ROTATION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,  0,
              static_cast<UINT>(offsetof(DX12GaussianSplatVertex, rotation)),
              D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT,     0,
              static_cast<UINT>(offsetof(DX12GaussianSplatVertex, color)),
              D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature   = root_sig;
        desc.VS               = { vs->blob->GetBufferPointer(), vs->blob->GetBufferSize() };
        desc.PS               = { ps->blob->GetBufferPointer(), ps->blob->GetBufferSize() };
        desc.InputLayout      = { layout, static_cast<UINT>(std::size(layout)) };
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        desc.RasterizerState  = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        D3D12_RENDER_TARGET_BLEND_DESC& rt = desc.BlendState.RenderTarget[0];
        rt.BlendEnable           = TRUE;
        rt.LogicOpEnable         = FALSE;
        rt.SrcBlend              = D3D12_BLEND_SRC_ALPHA;
        rt.DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
        rt.BlendOp               = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha         = D3D12_BLEND_ONE;
        rt.DestBlendAlpha        = D3D12_BLEND_INV_SRC_ALPHA;
        rt.BlendOpAlpha          = D3D12_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthEnable    = TRUE;
        desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        desc.DSVFormat         = DXGI_FORMAT_D32_FLOAT;

        desc.NumRenderTargets  = 1;
        desc.RTVFormats[0]     = get_backbuffer_format();
        desc.SampleMask        = UINT_MAX;
        desc.SampleDesc.Count  = 1;

        ID3D12PipelineState* pso = nullptr;
        HRESULT hr = get_device(device)->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&pso));

        if (FAILED(hr))
        {
            char buf[256];
            sprintf_s(buf,
                "create_pso_for_program(TerrainSurfelSurface) failed: 0x%08X\n",
                static_cast<unsigned>(hr));
            OutputDebugStringA(buf);
            return nullptr;
        }
        return pso;
    }

    static ID3D12PipelineState* create_sky_surface_pso(
        Device& device,
        ID3D12RootSignature* root_sig,
        GPUHandle vertex_shader,
        GPUHandle pixel_shader)
    {
        const DX12Shader* vs = get_shader(device, vertex_shader);
        const DX12Shader* ps = get_shader(device, pixel_shader);

        assert(vs && vs->blob);
        assert(ps && ps->blob);

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = root_sig;
        desc.VS = { vs->blob->GetBufferPointer(), vs->blob->GetBufferSize() };
        desc.PS = { ps->blob->GetBufferPointer(), ps->blob->GetBufferSize() };
        desc.InputLayout = { nullptr, 0 };
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        desc.DSVFormat = DXGI_FORMAT_UNKNOWN;

        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = get_backbuffer_format();
        desc.SampleMask = UINT_MAX;
        desc.SampleDesc.Count = 1;

        ID3D12PipelineState* pso = nullptr;
        HRESULT hr = get_device(device)->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&pso));

        if (FAILED(hr))
        {
            char buf[256];
            sprintf_s(buf,
                "create_pso_for_program(SkySurface) failed: 0x%08X\n",
                static_cast<unsigned>(hr));
            OutputDebugStringA(buf);
            return nullptr;
        }
        return pso;
    }

    ID3D12PipelineState* create_pso_for_program(
        Device& device,
        wz::engine::assets::BuiltinRenderProgram program,
        ID3D12RootSignature* root_sig,
        GPUHandle vertex_shader,
        GPUHandle pixel_shader)
    {
        using P = wz::engine::assets::BuiltinRenderProgram;
        switch (program)
        {
        case P::MeshWireframeDebug:
            return create_mesh_wireframe_pso(
                device,
                root_sig,
                vertex_shader,
                pixel_shader,
                false);
        case P::MeshWireframeDepthDebug:
            return create_mesh_wireframe_pso(
                device,
                root_sig,
                vertex_shader,
                pixel_shader,
                true);
        case P::MeshWireframeAlpha:
            return create_mesh_wireframe_pso(
                device,
                root_sig,
                vertex_shader,
                pixel_shader,
                true,
                true);
        case P::MeshDepthPrepassDebug:
            return create_mesh_depth_prepass_pso(
                device,
                root_sig,
                vertex_shader,
                pixel_shader);
        case P::MeshSurface:
            return create_terrain_mesh_surface_pso(
                device,
                root_sig,
                vertex_shader,
                pixel_shader,
                true);
        case P::MeshSurfaceAlpha:
            return create_terrain_mesh_surface_pso(
                device,
                root_sig,
                vertex_shader,
                pixel_shader,
                true,
                true);
        case P::MeshFieldHeatmap:
            return create_terrain_mesh_surface_pso(
                device,
                root_sig,
                vertex_shader,
                pixel_shader,
                true);
        case P::MeshMaskStyle:
            return create_terrain_mesh_surface_pso(
                device,
                root_sig,
                vertex_shader,
                pixel_shader,
                true);
        case P::TerrainMeshSurface:
            return create_terrain_mesh_surface_pso(
                device,
                root_sig,
                vertex_shader,
                pixel_shader,
                false);
        case P::GaussianSplatDebug:
            return create_gaussian_splat_pso(device, root_sig, vertex_shader, pixel_shader);
        case P::TerrainSurfelSurface:
            return create_terrain_surfel_pso(device, root_sig, vertex_shader, pixel_shader);
        case P::SkySurface:
            return create_sky_surface_pso(device, root_sig, vertex_shader, pixel_shader);
        default:
            return nullptr;
        }
    }

    // ── Data-driven pipeline creation ─────────────────────────────────────────

    namespace
    {
        static D3D12_SHADER_VISIBILITY to_dx12_visibility(
            wz::engine::assets::ShaderVisibility v)
        {
            using V = wz::engine::assets::ShaderVisibility;
            switch (v)
            {
            case V::Vertex: return D3D12_SHADER_VISIBILITY_VERTEX;
            case V::Pixel:  return D3D12_SHADER_VISIBILITY_PIXEL;
            default:        return D3D12_SHADER_VISIBILITY_ALL;
            }
        }

        static D3D12_DESCRIPTOR_RANGE_TYPE to_dx12_range_type(
            wz::engine::assets::DescriptorKind kind)
        {
            using K = wz::engine::assets::DescriptorKind;
            switch (kind)
            {
            case K::TextureSRV:          return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            case K::StructuredBufferSRV: return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            case K::Sampler:             return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
            case K::UAV:                 return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            }
            return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        }

        static std::vector<D3D12_INPUT_ELEMENT_DESC> build_input_layout(
            wz::engine::assets::InputLayoutKind kind)
        {
            using K = wz::engine::assets::InputLayoutKind;
            switch (kind)
            {
            case K::MeshPositionOnly:
                return {{
                    "POSITION", 0,
                    DXGI_FORMAT_R32G32B32_FLOAT,
                    0, 0,
                    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
                }};
            case K::MeshPositionNormalUV:
            {
                using Vertex = wz::engine::assets::MeshVertex;
                return {
                    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                    { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, normal)),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, static_cast<UINT>(offsetof(Vertex, uv)),       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                };
            }
            case K::GaussianSplatVertex:
                // Use offsetof — APPEND_ALIGNED_ELEMENT would skip pad0 between
                // scale and rotation, putting COLOR 4 bytes early (reading qw as red).
                return {
                    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,   0, static_cast<UINT>(offsetof(DX12GaussianSplatVertex, position)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
                    { "OPACITY",  0, DXGI_FORMAT_R32_FLOAT,          0, static_cast<UINT>(offsetof(DX12GaussianSplatVertex, opacity)),  D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
                    { "SCALE",    0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<UINT>(offsetof(DX12GaussianSplatVertex, scale)),    D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
                    { "ROTATION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(DX12GaussianSplatVertex, rotation)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
                    { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<UINT>(offsetof(DX12GaussianSplatVertex, color)),    D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
                };
            default:
                return {};
            }
        }
    }

    ID3D12RootSignature* create_root_signature_from_data(
        ID3D12Device* device,
        const wz::engine::assets::RenderProgramData& data)
    {
        // Group descriptor bindings by shader visibility: same-visibility bindings
        // share one root parameter (descriptor table) with one range per binding.
        // This lets a single SetGraphicsRootDescriptorTable call cover t0+t1 when
        // both are Vertex-visible, which is the SplatPull contract.
        //
        // Groups are built in first-occurrence order so root parameter indices are
        // stable and predictable from the declaration order in RenderProgramData.

        struct VisibilityGroup
        {
            D3D12_SHADER_VISIBILITY visibility{};
            std::vector<size_t>     binding_indices;  // indices into data.descriptor_bindings
        };

        auto can_share_descriptor_table =
            [&](const VisibilityGroup& group,
                const wz::engine::assets::DescriptorBinding& binding)
        {
            if (group.visibility != to_dx12_visibility(binding.visibility)) {
                return false;
            }
            if (binding.semantic
                    == wz::engine::assets::DescriptorSemantic::MeshMaskRules)
            {
                return false;
            }
            for (size_t group_binding_index : group.binding_indices) {
                if (data.descriptor_bindings[group_binding_index].semantic
                    == wz::engine::assets::DescriptorSemantic::MeshMaskRules)
                {
                    return false;
                }
            }
            return true;
        };

        std::vector<VisibilityGroup> groups;
        for (size_t i = 0; i < data.descriptor_bindings.size(); ++i)
        {
            const auto vis =
                to_dx12_visibility(data.descriptor_bindings[i].visibility);
            auto it = std::find_if(groups.begin(), groups.end(),
                [&](const VisibilityGroup& g)
                {
                    return can_share_descriptor_table(
                        g,
                        data.descriptor_bindings[i]);
                });
            if (it != groups.end())
                it->binding_indices.push_back(i);
            else
                groups.push_back({ vis, { i } });
        }

        // Build the ranges vector in group order so each group's ranges are
        // contiguous — required because pDescriptorRanges must point to a
        // contiguous array.
        std::vector<D3D12_DESCRIPTOR_RANGE> ranges(data.descriptor_bindings.size());
        {
            size_t ri = 0;
            for (const auto& group : groups)
            {
                for (size_t bi : group.binding_indices)
                {
                    const auto& db = data.descriptor_bindings[bi];
                    ranges[ri].RangeType                         = to_dx12_range_type(db.kind);
                    ranges[ri].NumDescriptors                    = db.descriptor_count;
                    ranges[ri].BaseShaderRegister                = db.shader_register;
                    ranges[ri].RegisterSpace                     = db.register_space;
                    ranges[ri].OffsetInDescriptorsFromTableStart =
                        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                    ++ri;
                }
            }
        }

        // Root parameters: root_constants first, then one table per visibility group.
        const size_t total_params = data.root_constants.size() + groups.size();
        std::vector<D3D12_ROOT_PARAMETER> params(total_params);
        size_t pi = 0;

        for (const auto& rc : data.root_constants)
        {
            D3D12_ROOT_PARAMETER& p = params[pi++];
            p.ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            p.ShaderVisibility         = to_dx12_visibility(rc.visibility);
            p.Constants.Num32BitValues = rc.value_count;
            p.Constants.ShaderRegister = rc.shader_register;
            p.Constants.RegisterSpace  = rc.register_space;
        }

        size_t range_start = 0;
        for (const auto& group : groups)
        {
            D3D12_ROOT_PARAMETER& p = params[pi++];
            p.ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p.ShaderVisibility                    = group.visibility;
            p.DescriptorTable.NumDescriptorRanges =
                static_cast<UINT>(group.binding_indices.size());
            p.DescriptorTable.pDescriptorRanges   = &ranges[range_start];
            range_start += group.binding_indices.size();
        }

        const bool uses_input_assembler =
            data.input_layout != wz::engine::assets::InputLayoutKind::None
            || data.binding_model
                == wz::engine::assets::RenderBindingModel::MeshVertexPull;

        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters     = static_cast<UINT>(params.size());
        desc.pParameters       = params.empty() ? nullptr : params.data();
        desc.NumStaticSamplers = 0;
        desc.pStaticSamplers   = nullptr;
        desc.Flags             = uses_input_assembler
            ? D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
            : D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ID3DBlob* sig_blob   = nullptr;
        ID3DBlob* error_blob = nullptr;
        HRESULT hr = D3D12SerializeRootSignature(
            &desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig_blob, &error_blob);

        if (FAILED(hr))
        {
            if (error_blob)
            {
                OutputDebugStringA(
                    static_cast<const char*>(error_blob->GetBufferPointer()));
                error_blob->Release();
            }
            return nullptr;
        }

        ID3D12RootSignature* root_sig = nullptr;
        hr = device->CreateRootSignature(
            0,
            sig_blob->GetBufferPointer(),
            sig_blob->GetBufferSize(),
            IID_PPV_ARGS(&root_sig));

        sig_blob->Release();
        if (error_blob) error_blob->Release();

        assert(SUCCEEDED(hr));
        return root_sig;
    }

    ID3D12PipelineState* create_pso_from_data(
        Device& device,
        const wz::engine::assets::RenderProgramData& data,
        ID3D12RootSignature* root_sig,
        GPUHandle vertex_shader,
        GPUHandle pixel_shader)
    {
        using BM = wz::rhi::BlendMode;
        using DM = wz::engine::assets::DepthMode;
        using RM = wz::engine::assets::RasterMode;

        const DX12Shader* vs = get_shader(device, vertex_shader);
        const DX12Shader* ps = get_shader(device, pixel_shader);

        assert(vs && vs->blob);
        assert(ps && ps->blob);

        auto layout = build_input_layout(data.input_layout);

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = root_sig;
        desc.VS             = { vs->blob->GetBufferPointer(), vs->blob->GetBufferSize() };
        desc.PS             = { ps->blob->GetBufferPointer(), ps->blob->GetBufferSize() };
        desc.InputLayout    = {
            layout.empty() ? nullptr : layout.data(),
            static_cast<UINT>(layout.size())
        };
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        switch (data.raster_mode)
        {
        case RM::SolidCullBack:
            desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
            break;
        case RM::SolidCullNone:
            desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            break;
        case RM::WireframeCullNone:
            desc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
            desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            break;
        }

        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        // ONE translation, shared with the rhi PSO path. This chain used
        // to be a private copy missing Additive / SourceAtop /
        // SliceFromDestination, so those three rendered opaque here and
        // correctly there (#317).
        desc.BlendState.RenderTarget[0] =
            render_target_blend_desc(data.blend_mode);

        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        switch (data.depth_mode)
        {
        case DM::Disabled:
            desc.DepthStencilState.DepthEnable    = FALSE;
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            desc.DSVFormat                        = DXGI_FORMAT_UNKNOWN;
            break;
        case DM::TestNoWrite:
            desc.DepthStencilState.DepthEnable    = TRUE;
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            desc.DSVFormat                        = DXGI_FORMAT_D32_FLOAT;
            break;
        case DM::TestWrite:
            desc.DepthStencilState.DepthEnable    = TRUE;
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            desc.DSVFormat                        = DXGI_FORMAT_D32_FLOAT;
            break;
        }

        desc.NumRenderTargets = 1;
        // The alpha-compositing modes draw ONTO the backbuffer, so they must
        // take its exact format; everything else targets a plain RGBA8 surface.
        desc.RTVFormats[0]    = (data.blend_mode == BM::AlphaBlend
                                 || data.blend_mode == BM::PremultipliedAlpha)
            ? get_backbuffer_format()
            : DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleMask       = UINT_MAX;
        desc.SampleDesc.Count = 1;

        ID3D12PipelineState* pso = nullptr;
        HRESULT hr = get_device(device)->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&pso));

        if (FAILED(hr))
        {
            char buf[256];
            sprintf_s(buf,
                "create_pso_from_data failed: 0x%08X\n",
                static_cast<unsigned>(hr));
            OutputDebugStringA(buf);
            return nullptr;
        }
        return pso;
    }
}
