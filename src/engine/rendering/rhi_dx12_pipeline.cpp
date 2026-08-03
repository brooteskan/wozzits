#include <engine/rendering/rhi_dx12_pipeline.h>

#include <gpu/dx12/dx12_internal.h>
#include <logging/logger.h>

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <d3d12.h>
#include <d3d12shader.h>
#include <d3dcompiler.h>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    std::string hr_hex(HRESULT hr)
    {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "0x%08lX",
            static_cast<unsigned long>(hr));
        return std::string(buffer);
    }

    D3D12_SHADER_VISIBILITY shader_visibility(wz::rhi::ShaderStage stage)
    {
        switch (stage) {
        case wz::rhi::ShaderStage::All:
            return D3D12_SHADER_VISIBILITY_ALL;
        case wz::rhi::ShaderStage::Vertex:
            return D3D12_SHADER_VISIBILITY_VERTEX;
        case wz::rhi::ShaderStage::Pixel:
            return D3D12_SHADER_VISIBILITY_PIXEL;
        case wz::rhi::ShaderStage::Compute:
            return D3D12_SHADER_VISIBILITY_ALL;
        }
        return D3D12_SHADER_VISIBILITY_ALL;
    }

    D3D12_STATIC_SAMPLER_DESC static_sampler_desc(
        const wz::rhi::StaticSamplerBinding& sampler)
    {
        D3D12_STATIC_SAMPLER_DESC desc{};
        switch (sampler.kind) {
        case wz::rhi::StaticSamplerKind::LinearClamp:
            desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            break;
        case wz::rhi::StaticSamplerKind::LinearWrap:
            desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            break;
        }
        desc.MipLODBias = 0.0f;
        desc.MaxAnisotropy = 1;
        desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        desc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
        desc.MinLOD = 0.0f;
        desc.MaxLOD = D3D12_FLOAT32_MAX;
        desc.ShaderRegister = sampler.shader_register;
        desc.RegisterSpace = sampler.register_space;
        desc.ShaderVisibility = shader_visibility(sampler.visibility);
        return desc;
    }

    D3D12_DESCRIPTOR_RANGE_TYPE descriptor_range_type(
        wz::rhi::DescriptorKind kind)
    {
        switch (kind) {
        case wz::rhi::DescriptorKind::StructuredBufferSRV:
        case wz::rhi::DescriptorKind::TextureSRV:
            return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        case wz::rhi::DescriptorKind::Sampler:
            return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        case wz::rhi::DescriptorKind::UAV:
            return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        }
        return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    }

    D3D12_SHADER_VISIBILITY descriptor_table_visibility(
        const wz::rhi::ShaderResourceGroupLayout& layout)
    {
        if (layout.descriptors.empty()) {
            return D3D12_SHADER_VISIBILITY_ALL;
        }
        const wz::rhi::ShaderStage first =
            layout.descriptors.front().visibility;
        if (!std::ranges::all_of(
                layout.descriptors,
                [first](const wz::rhi::DescriptorBinding& descriptor) {
                    return descriptor.visibility == first;
                }))
        {
            return D3D12_SHADER_VISIBILITY_ALL;
        }
        return shader_visibility(first);
    }

    DXGI_FORMAT vertex_format(wz::rhi::VertexFormat format)
    {
        switch (format) {
        case wz::rhi::VertexFormat::Float32:
            return DXGI_FORMAT_R32_FLOAT;
        case wz::rhi::VertexFormat::Float32x2:
            return DXGI_FORMAT_R32G32_FLOAT;
        case wz::rhi::VertexFormat::Float32x3:
            return DXGI_FORMAT_R32G32B32_FLOAT;
        case wz::rhi::VertexFormat::Float32x4:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case wz::rhi::VertexFormat::UInt32:
            return DXGI_FORMAT_R32_UINT;
        case wz::rhi::VertexFormat::UInt8x4Unorm:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        }
        return DXGI_FORMAT_R32G32B32_FLOAT;
    }

    std::vector<D3D12_INPUT_ELEMENT_DESC> build_input_layout(
        const wz::rhi::VertexLayout& layout)
    {
        std::vector<D3D12_INPUT_ELEMENT_DESC> out;
        out.reserve(layout.attributes.size());
        for (const wz::rhi::VertexAttribute& attribute : layout.attributes) {
            const wz::engine::rendering::Dx12InputElementSemantic semantic =
                wz::engine::rendering::dx12_input_element_semantic(
                    attribute.location);
            const bool per_instance =
                attribute.step != wz::rhi::VertexStepRate::PerVertex;
            out.push_back(D3D12_INPUT_ELEMENT_DESC{
                semantic.name,
                semantic.index,
                vertex_format(attribute.format),
                attribute.buffer_slot,
                attribute.offset,
                per_instance
                    ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                    : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                // InstanceDataStepRate must be 0 for per-vertex data and >= 1
                // for per-instance data -- 0 with PER_INSTANCE_DATA means the
                // attribute never advances. It was hardcoded 0 for both, which
                // the legacy factory (dx12_pipeline_factory.cpp) gets right
                // with 1; the duplicate-semantic failure below hid it, because
                // the PSO never got far enough to draw. See #317.
                per_instance ? 1u : 0u });
        }
        return out;
    }

    D3D12_FILL_MODE fill_mode(wz::rhi::RasterMode mode)
    {
        switch (mode) {
        case wz::rhi::RasterMode::SolidCullBack:
        case wz::rhi::RasterMode::SolidCullNone:
            return D3D12_FILL_MODE_SOLID;
        case wz::rhi::RasterMode::WireframeCullNone:
            return D3D12_FILL_MODE_WIREFRAME;
        }
        return D3D12_FILL_MODE_SOLID;
    }

    D3D12_CULL_MODE cull_mode(wz::rhi::RasterMode mode)
    {
        switch (mode) {
        case wz::rhi::RasterMode::SolidCullBack:
            return D3D12_CULL_MODE_BACK;
        case wz::rhi::RasterMode::SolidCullNone:
        case wz::rhi::RasterMode::WireframeCullNone:
            return D3D12_CULL_MODE_NONE;
        }
        return D3D12_CULL_MODE_BACK;
    }

    D3D12_PRIMITIVE_TOPOLOGY_TYPE topology_type(
        wz::rhi::PrimitiveTopology topology)
    {
        switch (topology) {
        case wz::rhi::PrimitiveTopology::TriangleList:
        case wz::rhi::PrimitiveTopology::TriangleStrip:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        }
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }

    D3D12_PRIMITIVE_TOPOLOGY primitive_topology(
        wz::rhi::PrimitiveTopology topology)
    {
        switch (topology) {
        case wz::rhi::PrimitiveTopology::TriangleList:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case wz::rhi::PrimitiveTopology::TriangleStrip:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        }
        return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }

    ID3D12RootSignature* create_root_signature(
        ID3D12Device* device,
        std::span<const wz::rhi::ShaderResourceGroupLayout>
            shader_resource_groups,
        bool allow_input_assembler,
        const wz::engine::rendering::RhiDx12PipelineLayout& layout,
        std::string_view program_name,
        wz::Logger* logger)
    {
        if (!device) {
            return nullptr;
        }

        std::vector<D3D12_ROOT_PARAMETER> params;
        params.resize(
            layout.descriptor_tables.size()
            + (layout.root_constants.valid ? 1u : 0u));

        std::vector<std::vector<CD3DX12_DESCRIPTOR_RANGE>> ranges;
        ranges.resize(layout.descriptor_tables.size());

        for (size_t table_index = 0;
             table_index < layout.descriptor_tables.size();
             ++table_index)
        {
            const auto& table = layout.descriptor_tables[table_index];
            const wz::rhi::ShaderResourceGroupLayout* srg =
                wz::rhi::find_shader_resource_group_layout(
                    shader_resource_groups,
                    table.binding_slot);
            if (!srg) {
                return nullptr;
            }

            ranges[table_index].reserve(srg->descriptors.size());
            for (const wz::rhi::DescriptorBinding& descriptor
                 : srg->descriptors)
            {
                CD3DX12_DESCRIPTOR_RANGE range;
                range.Init(
                    descriptor_range_type(descriptor.kind),
                    descriptor.descriptor_count,
                    descriptor.shader_register,
                    descriptor.register_space);
                ranges[table_index].push_back(range);
            }

            D3D12_ROOT_PARAMETER& param =
                params[table.root_parameter_index];
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.ShaderVisibility = descriptor_table_visibility(*srg);
            param.DescriptorTable.NumDescriptorRanges =
                static_cast<UINT>(ranges[table_index].size());
            param.DescriptorTable.pDescriptorRanges =
                ranges[table_index].data();
        }

        if (layout.root_constants.valid) {
            D3D12_ROOT_PARAMETER& param =
                params[layout.root_constants.root_parameter_index];
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            param.ShaderVisibility =
                shader_visibility(layout.root_constants.visibility);
            param.Constants.Num32BitValues = layout.root_constants.dword_count;
            param.Constants.ShaderRegister =
                layout.root_constants.shader_register;
            param.Constants.RegisterSpace =
                layout.root_constants.register_space;
        }

        // Static samplers are baked straight into the root signature — no
        // descriptor table, no sampler heap — from whatever the SRGs declare.
        // The register/space carried on each binding already matches the shader.
        std::vector<D3D12_STATIC_SAMPLER_DESC> static_samplers;
        for (const wz::rhi::ShaderResourceGroupLayout& srg
             : shader_resource_groups)
        {
            for (const wz::rhi::StaticSamplerBinding& sampler
                 : srg.static_samplers)
            {
                static_samplers.push_back(static_sampler_desc(sampler));
            }
        }

        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = static_cast<UINT>(params.size());
        desc.pParameters = params.data();
        desc.NumStaticSamplers = static_cast<UINT>(static_samplers.size());
        desc.pStaticSamplers =
            static_samplers.empty() ? nullptr : static_samplers.data();
        desc.Flags = allow_input_assembler
            ? D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
            : D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ID3DBlob* blob = nullptr;
        ID3DBlob* error = nullptr;
        HRESULT hr = D3D12SerializeRootSignature(
            &desc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &blob,
            &error);
        if (FAILED(hr)) {
            if (logger) {
                std::string detail;
                if (error) {
                    detail.assign(
                        static_cast<const char*>(error->GetBufferPointer()),
                        error->GetBufferSize());
                }
                logger->error(
                    std::string("RhiDx12Pipeline: root signature serialize "
                        "failed program=") + std::string(program_name)
                    + " hr=" + hr_hex(hr)
                    + (detail.empty() ? std::string{} : " error=" + detail));
            }
            if (error) {
                error->Release();
            }
            return nullptr;
        }

        ID3D12RootSignature* root_signature = nullptr;
        hr = device->CreateRootSignature(
            0,
            blob->GetBufferPointer(),
            blob->GetBufferSize(),
            IID_PPV_ARGS(&root_signature));
        blob->Release();
        if (error) {
            error->Release();
        }
        if (FAILED(hr)) {
            if (logger) {
                logger->error(
                    std::string("RhiDx12Pipeline: CreateRootSignature failed "
                        "program=") + std::string(program_name)
                    + " hr=" + hr_hex(hr));
            }
            return nullptr;
        }
        return root_signature;
    }

    ID3D12PipelineState* create_compute_pipeline_state(
        wz::gpu::Device& device,
        ID3D12RootSignature* root_signature,
        std::span<const uint8_t> compute_bytecode)
    {
        if (!root_signature || compute_bytecode.empty()) {
            return nullptr;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = root_signature;
        desc.CS = D3D12_SHADER_BYTECODE{
            compute_bytecode.data(),
            compute_bytecode.size() };

        ID3D12PipelineState* pso = nullptr;
        HRESULT hr = wz::gpu::dx12::internal::get_device(device)
            ->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pso));
        return SUCCEEDED(hr) ? pso : nullptr;
    }

    ID3D12PipelineState* create_pipeline_state(
        wz::gpu::Device& device,
        ID3D12RootSignature* root_signature,
        const wz::rhi::RenderProgramDesc& program,
        const wz::rhi::ProgramBytecode& bytecode,
        bool depth_target_bound,
        wz::Logger* logger)
    {
        if (!root_signature) {
            return nullptr;
        }

        std::vector<D3D12_INPUT_ELEMENT_DESC> input_layout;
        if (program.vertex_source == wz::rhi::VertexSource::InputAssembler) {
            input_layout = build_input_layout(program.vertex_layout);
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = root_signature;
        desc.VS = D3D12_SHADER_BYTECODE{
            bytecode.vertex.data(),
            bytecode.vertex.size() };
        desc.PS = D3D12_SHADER_BYTECODE{
            bytecode.pixel.data(),
            bytecode.pixel.size() };
        desc.InputLayout = D3D12_INPUT_LAYOUT_DESC{
            input_layout.data(),
            static_cast<UINT>(input_layout.size()) };
        desc.PrimitiveTopologyType = topology_type(program.topology);

        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.FillMode = fill_mode(program.raster_mode);
        desc.RasterizerState.CullMode = cull_mode(program.raster_mode);

        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        // ONE translation, shared with the legacy PSO factory
        // (dx12_pipeline_factory.cpp). Both used to carry their own
        // if/else chain; the other one silently lost three modes (#317).
        desc.BlendState.RenderTarget[0] =
            wz::gpu::dx12::internal::render_target_blend_desc(
                program.blend_mode);

        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        if (!depth_target_bound) {
            // The pass bound no DSV, so the only legal pipeline is one that
            // declares no depth format: D3D12 treats DSVFormat != UNKNOWN
            // against a null DSV as EXECUTION ERROR #615 and the draw is
            // undefined. This is the model begin_frame already documents from
            // the other side ("the bound DSV is then ignored" for a
            // depth-disabled program) -- the pipeline just never learned that
            // the offscreen pass binds none. The program's depth_mode is a
            // request that only means something when there is something to
            // test against. See #317.
            desc.DepthStencilState.DepthEnable = FALSE;
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
        }
        else
        switch (program.depth_mode) {
        case wz::rhi::DepthMode::Disabled:
            desc.DepthStencilState.DepthEnable = FALSE;
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
            break;
        case wz::rhi::DepthMode::TestNoWrite:
            desc.DepthStencilState.DepthEnable = TRUE;
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            break;
        case wz::rhi::DepthMode::TestWrite:
            desc.DepthStencilState.DepthEnable = TRUE;
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            break;
        }

        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = wz::gpu::dx12::internal::get_backbuffer_format();
        desc.SampleMask = UINT_MAX;
        desc.SampleDesc.Count = 1;

        ID3D12PipelineState* pso = nullptr;
        HRESULT hr = wz::gpu::dx12::internal::get_device(device)
            ->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso));
        if (FAILED(hr)) {
            if (logger) {
                logger->error(
                    std::string("RhiDx12Pipeline: CreateGraphicsPipelineState "
                        "failed program=") + program.name
                    + " hr=" + hr_hex(hr));
            }
            return nullptr;
        }
        return pso;
    }
}

namespace wz::engine::rendering
{
    std::optional<uint32_t> RhiDx12PipelineLayout::root_param_for_slot(
        uint32_t binding_slot) const noexcept
    {
        const auto table = std::ranges::find_if(
            descriptor_tables,
            [binding_slot](const RhiDx12DescriptorTableParam& candidate) {
                return candidate.binding_slot == binding_slot;
            });
        if (table != descriptor_tables.end()) {
            return table->root_parameter_index;
        }
        return std::nullopt;
    }

    std::vector<std::string> shader_binding_disagreements(
        std::span<const wz::rhi::ShaderResourceGroupLayout>
            shader_resource_groups,
        std::span<const uint8_t> bytecode,
        std::string_view stage)
    {
        std::vector<std::string> out;
        if (bytecode.empty()) {
            return out;
        }

        ID3D12ShaderReflection* reflection = nullptr;
        if (FAILED(D3DReflect(
                bytecode.data(),
                bytecode.size(),
                IID_PPV_ARGS(&reflection)))
            || !reflection)
        {
            // Reflection data can legitimately be absent (a stripped blob);
            // that is not a disagreement.
            return out;
        }

        D3D12_SHADER_DESC shader{};
        if (FAILED(reflection->GetDesc(&shader))) {
            reflection->Release();
            return out;
        }

        const std::string prefix = std::string(stage) + " shader: ";
        const auto at = [](uint32_t reg, uint32_t space) {
            return "register " + std::to_string(reg) + " space "
                + std::to_string(space);
        };

        for (UINT i = 0; i < shader.BoundResources; ++i) {
            D3D12_SHADER_INPUT_BIND_DESC bind{};
            if (FAILED(reflection->GetResourceBindingDesc(i, &bind))) {
                continue;
            }
            const std::string name = bind.Name ? bind.Name : "<unnamed>";

            if (bind.Type == D3D_SIT_CBUFFER) {
                // The one thing D3D12 itself will not check. A root signature
                // declaring fewer DWORDs than the shader's cbuffer occupies is
                // ACCEPTED by CreateGraphicsPipelineState; the shader then
                // reads past what was ever written.
                const wz::rhi::ShaderResourceGroupLayout* group = nullptr;
                for (const wz::rhi::ShaderResourceGroupLayout& candidate :
                     shader_resource_groups)
                {
                    if (!candidate.constants.empty()
                        && candidate.constants_binding.shader_register
                            == bind.BindPoint
                        && candidate.constants_binding.register_space
                            == bind.Space)
                    {
                        group = &candidate;
                        break;
                    }
                }
                if (!group) {
                    out.push_back(
                        prefix + "declares cbuffer '" + name + "' at "
                        + at(bind.BindPoint, bind.Space)
                        + ", which the layout does not declare");
                    continue;
                }
                ID3D12ShaderReflectionConstantBuffer* cb =
                    reflection->GetConstantBufferByName(name.c_str());
                D3D12_SHADER_BUFFER_DESC cb_desc{};
                if (cb && SUCCEEDED(cb->GetDesc(&cb_desc))) {
                    // Compare the last DECLARED FIELD, not the buffer's size.
                    // HLSL pads a cbuffer to a float4 boundary, so a block of
                    // 39 declared dwords reports Size == 40 -- measured on the
                    // shipped HutConstants (39 -> 40) and FlashConstants
                    // (17 -> 20). Comparing sizes flags every well-formed
                    // program in the tree; comparing field extents flags only a
                    // field the root signature will not write.
                    uint32_t last_field_dword = 0;
                    for (UINT v = 0; v < cb_desc.Variables; ++v) {
                        ID3D12ShaderReflectionVariable* var =
                            cb->GetVariableByIndex(v);
                        D3D12_SHADER_VARIABLE_DESC var_desc{};
                        if (!var || FAILED(var->GetDesc(&var_desc))) {
                            continue;
                        }
                        const uint32_t end_dword =
                            (var_desc.StartOffset + var_desc.Size + 3u) / 4u;
                        last_field_dword =
                            end_dword > last_field_dword
                                ? end_dword
                                : last_field_dword;
                    }
                    const uint32_t layout_dwords =
                        group->constants.dword_count();
                    if (last_field_dword > layout_dwords) {
                        out.push_back(
                            prefix + "cbuffer '" + name + "' at "
                            + at(bind.BindPoint, bind.Space)
                            + " declares fields out to dword "
                            + std::to_string(last_field_dword)
                            + " but the layout supplies only "
                            + std::to_string(layout_dwords)
                            + " — the last "
                            + std::to_string(last_field_dword - layout_dwords)
                            + " dword(s) are read but never written");
                    }
                }
                continue;
            }

            if (bind.Type == D3D_SIT_SAMPLER) {
                bool declared = false;
                for (const wz::rhi::ShaderResourceGroupLayout& group :
                     shader_resource_groups)
                {
                    for (const wz::rhi::StaticSamplerBinding& sampler :
                         group.static_samplers)
                    {
                        declared = declared
                            || (sampler.shader_register == bind.BindPoint
                                && sampler.register_space == bind.Space);
                    }
                    for (const wz::rhi::DescriptorBinding& descriptor :
                         group.descriptors)
                    {
                        declared = declared
                            || (descriptor.kind
                                    == wz::rhi::DescriptorKind::Sampler
                                && descriptor.shader_register == bind.BindPoint
                                && descriptor.register_space == bind.Space);
                    }
                }
                if (!declared) {
                    out.push_back(
                        prefix + "samples '" + name + "' at "
                        + at(bind.BindPoint, bind.Space)
                        + ", which the layout does not declare");
                }
                continue;
            }

            // SRV / UAV.
            const wz::rhi::DescriptorBinding* declared = nullptr;
            for (const wz::rhi::ShaderResourceGroupLayout& group :
                 shader_resource_groups)
            {
                for (const wz::rhi::DescriptorBinding& descriptor :
                     group.descriptors)
                {
                    if (descriptor.shader_register == bind.BindPoint
                        && descriptor.register_space == bind.Space
                        && descriptor.kind != wz::rhi::DescriptorKind::Sampler)
                    {
                        declared = &descriptor;
                        break;
                    }
                }
                if (declared) {
                    break;
                }
            }
            if (!declared) {
                out.push_back(
                    prefix + "binds '" + name + "' at "
                    + at(bind.BindPoint, bind.Space)
                    + ", which the layout does not declare");
                continue;
            }

            // The layout builds the view; if the shader wants a different
            // shape at that register it reads the right memory the wrong way.
            const bool wants_texture =
                bind.Type == D3D_SIT_TEXTURE;
            const bool wants_uav =
                bind.Type == D3D_SIT_UAV_RWTYPED
                || bind.Type == D3D_SIT_UAV_RWSTRUCTURED
                || bind.Type == D3D_SIT_UAV_RWBYTEADDRESS
                || bind.Type == D3D_SIT_UAV_APPEND_STRUCTURED
                || bind.Type == D3D_SIT_UAV_CONSUME_STRUCTURED
                || bind.Type == D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER;
            const wz::rhi::DescriptorKind expected =
                wants_uav      ? wz::rhi::DescriptorKind::UAV
                : wants_texture ? wz::rhi::DescriptorKind::TextureSRV
                                : wz::rhi::DescriptorKind::StructuredBufferSRV;
            if (declared->kind != expected) {
                const auto kind_name = [](wz::rhi::DescriptorKind kind) {
                    switch (kind) {
                    case wz::rhi::DescriptorKind::StructuredBufferSRV:
                        return "StructuredBufferSRV";
                    case wz::rhi::DescriptorKind::TextureSRV:
                        return "TextureSRV";
                    case wz::rhi::DescriptorKind::Sampler:
                        return "Sampler";
                    case wz::rhi::DescriptorKind::UAV:
                        return "UAV";
                    }
                    return "?";
                };
                out.push_back(
                    prefix + "binds '" + name + "' at "
                    + at(bind.BindPoint, bind.Space) + " as "
                    + kind_name(expected) + " but the layout declares "
                    + kind_name(declared->kind)
                    + " — the descriptor built there views the memory the "
                      "wrong way");
            }
        }

        reflection->Release();
        return out;
    }

    std::optional<RhiDx12PipelineLayout> plan_dx12_pipeline_layout(
        std::span<const wz::rhi::ShaderResourceGroupLayout>
            shader_resource_groups)
    {
        if (!wz::rhi::shader_resource_group_slots_are_unique(
                shader_resource_groups))
        {
            return std::nullopt;
        }

        std::vector<const wz::rhi::ShaderResourceGroupLayout*> srgs;
        srgs.reserve(shader_resource_groups.size());
        for (const wz::rhi::ShaderResourceGroupLayout& srg
             : shader_resource_groups)
        {
            srgs.push_back(&srg);
        }
        std::sort(srgs.begin(), srgs.end(),
            [](const auto* a, const auto* b) {
                return a->binding_slot < b->binding_slot;
            });

        RhiDx12PipelineLayout layout;
        uint32_t next_root_param = 0;
        for (const wz::rhi::ShaderResourceGroupLayout* srg : srgs) {
            if (!srg->descriptors.empty()) {
                uint32_t descriptor_count = 0;
                for (const wz::rhi::DescriptorBinding& descriptor
                     : srg->descriptors)
                {
                    descriptor_count += descriptor.descriptor_count;
                }
                layout.descriptor_tables.push_back({
                    srg->binding_slot,
                    next_root_param++,
                    descriptor_count,
                    {} });
                layout.descriptor_tables.back().descriptor_kinds.reserve(
                    srg->descriptors.size());
                for (const wz::rhi::DescriptorBinding& descriptor
                     : srg->descriptors)
                {
                    layout.descriptor_tables.back().descriptor_kinds.push_back(
                        descriptor.kind);
                }
            }
        }

        for (const wz::rhi::ShaderResourceGroupLayout* srg : srgs) {
            if (srg->constants.empty()) {
                continue;
            }
            if (layout.root_constants.valid) {
                return std::nullopt;
            }
            layout.root_constants = RhiDx12RootConstantsParam{
                true,
                next_root_param++,
                srg->constants.dword_count(),
                srg->constants_binding.shader_register,
                srg->constants_binding.register_space,
                srg->constants_binding.visibility };
        }

        // A D3D12 root signature may total at most 64 DWORDs: one per 32-bit
        // root constant, one per descriptor table. Nothing bounded this
        // anywhere -- constants_dwords is an authored integer with only a LOWER
        // bound ("smaller than head + declared fields") -- so an over-budget
        // layout reached CreateRootSignature and came back E_INVALIDARG with no
        // attribution. Worse, D3D12SerializeRootSignature succeeds for these,
        // so the one place we capture a D3D error blob never fires and the
        // failure arrives as a bare hr. Measured: 60 constants + 1 table is
        // S_OK with 3 DWORDs to spare (that is the shipped
        // GaussianSplatTerrainCoverageDebug preset); 64 + 1 table is
        // E_INVALIDARG. See #317.
        //
        // Checked HERE rather than in wozzits-rhi because 64 is a D3D12 limit,
        // not a property of the device-free contract -- but the cost model is
        // simple enough to state at plan time, which is the last point where
        // the program is still identifiable.
        if (dx12_root_signature_dword_cost(layout)
            > kDx12MaxRootSignatureDwords)
        {
            return std::nullopt;
        }

        return layout;
    }

    RhiDx12PipelineCache::RhiDx12PipelineCache(
        wz::gpu::Device& device,
        const wz::rhi::RenderProgramRegistry& programs,
        const wz::rhi::ComputeProgramRegistry& compute_programs,
        const wz::rhi::ShaderModuleRegistry& shaders,
        wz::Logger* logger)
        : device_(&device)
        , programs_(&programs)
        , compute_programs_(&compute_programs)
        , shaders_(&shaders)
        , logger_(logger)
    {
    }

    RhiDx12PipelineCache::~RhiDx12PipelineCache()
    {
        clear();
    }

    const RhiDx12RealizedPipeline* RhiDx12PipelineCache::get(
        wz::rhi::Tag program) const noexcept
    {
        return device_ ? get(program,
                             wz::gpu::dx12::internal::depth_target_bound(
                                 *device_))
                       : nullptr;
    }

    const RhiDx12RealizedPipeline* RhiDx12PipelineCache::get(
        wz::rhi::Tag program, bool depth_target_bound) const noexcept
    {
        const auto entry = std::ranges::find_if(
            entries_,
            [program, depth_target_bound](const Entry& candidate) {
                return candidate.program == program
                    && candidate.depth_target_bound == depth_target_bound;
            });
        if (entry != entries_.end()) {
            return &entry->realized;
        }
        return nullptr;
    }

    const RhiDx12RealizedPipeline* RhiDx12PipelineCache::realize(
        wz::rhi::Tag program)
    {
        if (!device_ || !programs_ || !shaders_ || !program.valid()) {
            return nullptr;
        }

        // Part of the key, not of the program: whether the pass we are about to
        // record into bound a depth-stencil view. One program drawn into the
        // backbuffer and into an authored render target needs two pipelines,
        // because D3D12 refuses a non-UNKNOWN DSVFormat against a null DSV.
        // Read from the device rather than plumbed through the renderer so it
        // cannot disagree with what OMSetRenderTargets actually did (#317).
        const bool depth_bound =
            wz::gpu::dx12::internal::depth_target_bound(*device_);

        if (const RhiDx12RealizedPipeline* existing =
                get(program, depth_bound)) {
            return existing;
        }

        if (const wz::rhi::RenderProgramDesc* desc = programs_->get(program)) {
            // The backend does not implement the input-assembler vertex path:
            // RhiDx12CommandRecorder::set_geometry binds nothing, so a program
            // declaring it realizes a pipeline with a real input layout, records
            // a packet, reports ready() == true, and draws from vertex buffers
            // that were never bound. Silent, and the wrong kind of silent -- a
            // blank mesh with no diagnostic anywhere (#317 D1-Q1).
            //
            // The declaration STAYS: VertexSource::InputAssembler and the vertex
            // layout vocabulary around it are kept design surface for planned
            // work, not dead code. What is refused is USING it, until
            // set_geometry is implemented -- which is the same shape the recorder
            // already applies to DescriptorKind::Sampler.
            //
            // Refusing here rather than at the packet is deliberate: this is the
            // moment the program is still identifiable by name.
            //
            // It is REACHABLE BY ACCIDENT rather than by choice, which is why it
            // needs to be loud: InputAssembler is RenderProgramDesc's DEFAULT
            // (render_program.h), so a program that never mentions vertex_source
            // gets it. Corpus: 0 of 16 registered programs in test_mesh_001 use
            // it, and 0 of 90 real packets name a vertex stream, so nothing
            // shipping is refused by this.
            if (desc->vertex_source == wz::rhi::VertexSource::InputAssembler) {
                if (logger_) {
                    logger_->error(
                        std::string("RhiDx12PipelineCache::realize: this backend "
                            "does not implement VertexSource::InputAssembler "
                            "(set_geometry binds no buffers), so the draw would "
                            "read unbound vertex data — declare a pull vertex "
                            "source instead. program=")
                        + std::string(desc->name));
                }
                return nullptr;
            }

            const std::optional<wz::rhi::ProgramBytecode> bytecode =
                wz::rhi::resolve_program_bytecode(*desc, *shaders_);
            if (!bytecode) {
                if (logger_) {
                    logger_->error(
                        std::string("RhiDx12PipelineCache::realize: shader "
                            "bytecode unavailable (a shader module is not "
                            "registered) — program=") + std::string(desc->name));
                }
                return nullptr;
            }

            const std::optional<RhiDx12PipelineLayout> layout =
                plan_dx12_pipeline_layout(*desc);
            if (!layout) {
                if (logger_) {
                    logger_->error(
                        std::string("RhiDx12PipelineCache::realize: pipeline "
                            "layout planning failed — program=")
                        + std::string(desc->name));
                }
                return nullptr;
            }

            // Does the shader agree with the layout that will be bound to it?
            // Nothing else asks: not rhi, not the bridge, not the root
            // signature, and not the D3D12 runtime, which accepts a root
            // signature declaring 4 constants against a 20-dword cbuffer.
            //
            // Fails CLOSED because the alternative is silent: a shader reading
            // dwords the root signature never writes renders garbage with no
            // error anywhere. Checked BEFORE the root signature is created so
            // there is nothing to release on the way out.
            //
            // Corpus-checked before landing: zero disagreements across
            // test_mesh_001 (16 programs), test_rebind_fixture and
            // glb_scene_source_fixture, so nothing shipping is refused today.
            // Reflection data can legitimately be absent from a stripped blob,
            // and then this reports nothing -- it cannot check, so it does not
            // refuse.
            {
                std::vector<std::string> disagreements =
                    shader_binding_disagreements(
                        desc->shader_resource_groups,
                        bytecode->vertex,
                        "vertex");
                for (std::string& pixel_disagreement :
                     shader_binding_disagreements(
                         desc->shader_resource_groups,
                         bytecode->pixel,
                         "pixel"))
                {
                    disagreements.push_back(std::move(pixel_disagreement));
                }
                if (!disagreements.empty()) {
                    if (logger_) {
                        for (const std::string& disagreement : disagreements) {
                            logger_->error(
                                std::string("RhiDx12PipelineCache::realize: ")
                                + std::string(desc->name) + ": "
                                + disagreement);
                        }
                    }
                    return nullptr;
                }
            }

            ID3D12Device* d3d = wz::gpu::dx12::internal::get_device(*device_);
            ID3D12RootSignature* root_signature =
                create_root_signature(
                    d3d,
                    desc->shader_resource_groups,
                    /*allow_input_assembler*/ true,
                    *layout,
                    desc->name,
                    logger_);
            if (!root_signature) {
                return nullptr;
            }

            ID3D12PipelineState* pso = create_pipeline_state(
                *device_,
                root_signature,
                *desc,
                *bytecode,
                depth_bound,
                logger_);
            if (!pso) {
                root_signature->Release();
                return nullptr;
            }

            entries_.push_back(Entry{
                program,
                depth_bound,
                RhiDx12RealizedPipeline{
                    root_signature,
                    pso,
                    *layout,
                    static_cast<uint32_t>(primitive_topology(desc->topology)),
                    false } });
            return &entries_.back().realized;
        }

        const wz::rhi::ComputeProgramDesc* compute_desc =
            compute_programs_ ? compute_programs_->get(program) : nullptr;
        if (!compute_desc) {
            if (logger_) {
                logger_->error(
                    "RhiDx12PipelineCache::realize: program tag not registered "
                    "in the rhi program registry (graphics + compute miss) — a "
                    "render program's rhi registration is missing at render "
                    "time");
            }
            return nullptr;
        }

        const std::optional<std::vector<uint8_t>> compute_bytecode =
            wz::rhi::resolve_compute_bytecode(*compute_desc, *shaders_);
        if (!compute_bytecode) {
            return nullptr;
        }

        const std::optional<RhiDx12PipelineLayout> layout =
            plan_dx12_pipeline_layout(*compute_desc);
        if (!layout) {
            return nullptr;
        }

        ID3D12Device* d3d = wz::gpu::dx12::internal::get_device(*device_);
        ID3D12RootSignature* root_signature =
            create_root_signature(
                d3d,
                compute_desc->shader_resource_groups,
                /*allow_input_assembler*/ false,
                *layout,
                compute_desc->name,
                logger_);
        if (!root_signature) {
            return nullptr;
        }

        ID3D12PipelineState* pso = create_compute_pipeline_state(
            *device_,
            root_signature,
            *compute_bytecode);
        if (!pso) {
            root_signature->Release();
            return nullptr;
        }

        entries_.push_back(Entry{
            program,
            // A compute pipeline has no render targets, so the depth key is
            // irrelevant to it; false keeps one entry per compute program.
            /*depth_target_bound*/ false,
            RhiDx12RealizedPipeline{
                root_signature,
                pso,
                *layout,
                /*primitive_topology*/ 0,
                true } });
        return &entries_.back().realized;
    }

    void RhiDx12PipelineCache::clear() noexcept
    {
        for (Entry& entry : entries_) {
            if (entry.realized.pipeline_state) {
                entry.realized.pipeline_state->Release();
                entry.realized.pipeline_state = nullptr;
            }
            if (entry.realized.root_signature) {
                entry.realized.root_signature->Release();
                entry.realized.root_signature = nullptr;
            }
        }
        entries_.clear();
    }
}
