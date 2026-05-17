// src/gpu/dx12/dx12_pipeline_factory.cpp
//
// Single source of truth for built-in DX12 root signature and PSO creation.
// The legacy debug context files delegate to these functions during the
// transition to RenderablePipelineCache.

#include <gpu/dx12/dx12_pipeline_factory.h>
#include <gpu/dx12/dx12_internal.h>

#include "dx12_device_internal.h"

#include <cassert>

namespace wz::gpu::dx12::internal
{
    // ── Root signatures ───────────────────────────────────────────────────────

    static ID3D12RootSignature* create_mesh_wireframe_root_sig(ID3D12Device* device)
    {
        // 32 × 32-bit constants (world[16] + view_proj[16]), register 0, VS only.
        return create_empty_root_signature(device);
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

    ID3D12RootSignature* create_root_signature_for_program(
        ID3D12Device* device,
        wz::engine::assets::BuiltinRenderProgram program)
    {
        using P = wz::engine::assets::BuiltinRenderProgram;
        switch (program)
        {
        case P::MeshWireframeDebug:
            return create_mesh_wireframe_root_sig(device);
        case P::GaussianSplatDebug:
            return create_gaussian_splat_root_sig(device);
        default:
            return nullptr;
        }
    }

    // ── PSOs ──────────────────────────────────────────────────────────────────

    static ID3D12PipelineState* create_mesh_wireframe_pso(
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
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

        desc.BlendState        = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.DSVFormat         = DXGI_FORMAT_UNKNOWN;

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

        D3D12_INPUT_ELEMENT_DESC layout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "SCALE",    0, DXGI_FORMAT_R32_FLOAT,        0, 12, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT,  0, 16, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "OPACITY",  0, DXGI_FORMAT_R32_FLOAT,        0, 28, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
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
            return create_mesh_wireframe_pso(device, root_sig, vertex_shader, pixel_shader);
        case P::GaussianSplatDebug:
            return create_gaussian_splat_pso(device, root_sig, vertex_shader, pixel_shader);
        default:
            return nullptr;
        }
    }
}
