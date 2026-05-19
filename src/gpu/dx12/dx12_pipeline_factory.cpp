// src/gpu/dx12/dx12_pipeline_factory.cpp
//
// Single source of truth for built-in DX12 root signature and PSO creation.
// The legacy debug context files delegate to these functions during the
// transition to RenderablePipelineCache.

#include <gpu/dx12/dx12_pipeline_factory.h>
#include <gpu/dx12/dx12_internal.h>

#include "dx12_device_internal.h"

#include <cassert>
#include <vector>

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
            {
                "POSITION", 0,
                DXGI_FORMAT_R32G32B32_FLOAT,
                0,
                D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,
                1
            },
            {
                "OPACITY", 0,
                DXGI_FORMAT_R32_FLOAT,
                0,
                D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,
                1
            },
            {
                "SCALE", 0,
                DXGI_FORMAT_R32G32B32_FLOAT,
                0,
                D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,
                1
            },
            {
                "ROTATION", 0,
                DXGI_FORMAT_R32G32B32A32_FLOAT,
                0,
                D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,
                1
            },
            {
                "COLOR", 0,
                DXGI_FORMAT_R32G32B32_FLOAT,
                0,
                D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,
                1
            },
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

        static D3D12_ROOT_SIGNATURE_FLAGS ia_flag_for_binding_model(
            wz::engine::assets::RenderBindingModel model)
        {
            using M = wz::engine::assets::RenderBindingModel;
            if (model == M::MeshIA || model == M::SplatVertexInstanced)
                return D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
            return D3D12_ROOT_SIGNATURE_FLAG_NONE;
        }

        static std::vector<D3D12_INPUT_ELEMENT_DESC> build_input_layout(
            wz::engine::assets::RenderBindingModel model)
        {
            using M = wz::engine::assets::RenderBindingModel;
            switch (model)
            {
            case M::MeshIA:
                return {{
                    "POSITION", 0,
                    DXGI_FORMAT_R32G32B32_FLOAT,
                    0, 0,
                    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
                }};
            case M::SplatVertexInstanced:
                return {
                    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
                    { "OPACITY",  0, DXGI_FORMAT_R32_FLOAT,           0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
                    { "SCALE",    0, DXGI_FORMAT_R32G32B32_FLOAT,     0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
                    { "ROTATION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,  0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
                    { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT,     0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
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
        using K = wz::engine::assets::ShaderResourceKind;

        // Two-pass: build descriptor ranges first so their addresses stay stable
        // when we point D3D12_ROOT_PARAMETER::DescriptorTable at them.
        struct RangeEntry { D3D12_DESCRIPTOR_RANGE range; size_t param_index; };
        std::vector<RangeEntry> range_storage;
        range_storage.reserve(data.bindings.size());

        for (size_t i = 0; i < data.bindings.size(); ++i)
        {
            const auto& b = data.bindings[i];
            if (b.kind == K::StructuredBuffer || b.kind == K::Texture2D)
            {
                D3D12_DESCRIPTOR_RANGE r{};
                r.RangeType                         = (b.kind == K::Texture2D)
                    ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV
                    : D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                r.NumDescriptors                    = (b.count > 0) ? b.count : 1;
                r.BaseShaderRegister                = b.shader_register;
                r.RegisterSpace                     = b.register_space;
                r.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                range_storage.push_back({ r, i });
            }
            else if (b.kind == K::Sampler)
            {
                D3D12_DESCRIPTOR_RANGE r{};
                r.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                r.NumDescriptors                    = (b.count > 0) ? b.count : 1;
                r.BaseShaderRegister                = b.shader_register;
                r.RegisterSpace                     = b.register_space;
                r.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                range_storage.push_back({ r, i });
            }
        }

        std::vector<D3D12_ROOT_PARAMETER> params(data.bindings.size());
        for (size_t i = 0; i < data.bindings.size(); ++i)
        {
            const auto& b = data.bindings[i];
            D3D12_ROOT_PARAMETER& p = params[i];
            p.ShaderVisibility = to_dx12_visibility(b.visibility);

            if (b.kind == K::ConstantBuffer)
            {
                p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                p.Constants.Num32BitValues = b.count;
                p.Constants.ShaderRegister = b.shader_register;
                p.Constants.RegisterSpace  = b.register_space;
            }
            else
            {
                // SRV / UAV / Sampler go in a one-range descriptor table.
                const RangeEntry* entry = nullptr;
                for (const auto& re : range_storage)
                    if (re.param_index == i) { entry = &re; break; }

                assert(entry);
                p.ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                p.DescriptorTable.NumDescriptorRanges = 1;
                p.DescriptorTable.pDescriptorRanges   = &entry->range;
            }
        }

        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters   = static_cast<UINT>(params.size());
        desc.pParameters     = params.empty() ? nullptr : params.data();
        desc.NumStaticSamplers = 0;
        desc.pStaticSamplers = nullptr;
        desc.Flags           = ia_flag_for_binding_model(data.binding_model);

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
        const DX12Shader* vs = get_shader(device, vertex_shader);
        const DX12Shader* ps = get_shader(device, pixel_shader);

        assert(vs && vs->blob);
        assert(ps && ps->blob);

        auto layout = build_input_layout(data.binding_model);

        const bool wireframe =
            (data.default_policy_flags & wz::engine::assets::RenderPolicy_Wireframe) != 0;
        const bool alpha_blend =
            (data.default_policy_flags & wz::engine::assets::RenderPolicy_AlphaBlend) != 0;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = root_sig;
        desc.VS             = { vs->blob->GetBufferPointer(), vs->blob->GetBufferSize() };
        desc.PS             = { ps->blob->GetBufferPointer(), ps->blob->GetBufferSize() };
        desc.InputLayout    = {
            layout.empty() ? nullptr : layout.data(),
            static_cast<UINT>(layout.size())
        };
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        desc.RasterizerState          = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.FillMode = wireframe
            ? D3D12_FILL_MODE_WIREFRAME
            : D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        if (alpha_blend)
        {
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

        desc.DepthStencilState            = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.DSVFormat                    = DXGI_FORMAT_UNKNOWN;

        desc.NumRenderTargets = 1;
        desc.RTVFormats[0]    = alpha_blend
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
