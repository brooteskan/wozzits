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

        std::vector<VisibilityGroup> groups;
        for (size_t i = 0; i < data.descriptor_bindings.size(); ++i)
        {
            const auto vis = to_dx12_visibility(data.descriptor_bindings[i].visibility);
            auto it = std::find_if(groups.begin(), groups.end(),
                [vis](const VisibilityGroup& g) { return g.visibility == vis; });
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

        const bool has_ia = (data.input_layout != wz::engine::assets::InputLayoutKind::None);

        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters     = static_cast<UINT>(params.size());
        desc.pParameters       = params.empty() ? nullptr : params.data();
        desc.NumStaticSamplers = 0;
        desc.pStaticSamplers   = nullptr;
        desc.Flags             = has_ia
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
        using BM = wz::engine::assets::BlendMode;
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
        if (data.blend_mode == BM::AlphaBlend)
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
        desc.RTVFormats[0]    = (data.blend_mode == BM::AlphaBlend)
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
