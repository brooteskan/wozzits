#include <engine/rendering/rhi_dx12_pipeline.h>

#include <gpu/dx12/dx12_internal.h>

#include <algorithm>
#include <climits>
#include <cstddef>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <optional>
#include <utility>

namespace
{
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
        const wz::rhi::ShaderStage first = layout.descriptors.front().visibility;
        for (const wz::rhi::DescriptorBinding& descriptor : layout.descriptors) {
            if (descriptor.visibility != first) {
                return D3D12_SHADER_VISIBILITY_ALL;
            }
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

    const char* semantic_name(uint32_t location)
    {
        switch (location) {
        case 0: return "POSITION";
        case 1: return "NORMAL";
        case 2: return "TEXCOORD";
        case 3: return "COLOR";
        default: return "TEXCOORD";
        }
    }

    std::vector<D3D12_INPUT_ELEMENT_DESC> build_input_layout(
        const wz::rhi::VertexLayout& layout)
    {
        std::vector<D3D12_INPUT_ELEMENT_DESC> out;
        out.reserve(layout.attributes.size());
        for (const wz::rhi::VertexAttribute& attribute : layout.attributes) {
            out.push_back(D3D12_INPUT_ELEMENT_DESC{
                semantic_name(attribute.location),
                0,
                vertex_format(attribute.format),
                attribute.buffer_slot,
                attribute.offset,
                attribute.step == wz::rhi::VertexStepRate::PerVertex
                    ? D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA
                    : D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,
                0 });
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
        const wz::rhi::RenderProgramDesc& program,
        const wz::engine::rendering::RhiDx12PipelineLayout& layout)
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
                    program.shader_resource_groups,
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

        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = static_cast<UINT>(params.size());
        desc.pParameters = params.data();
        desc.NumStaticSamplers = 0;
        desc.pStaticSamplers = nullptr;
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ID3DBlob* blob = nullptr;
        ID3DBlob* error = nullptr;
        HRESULT hr = D3D12SerializeRootSignature(
            &desc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &blob,
            &error);
        if (FAILED(hr)) {
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
        return SUCCEEDED(hr) ? root_signature : nullptr;
    }

    ID3D12PipelineState* create_pipeline_state(
        wz::gpu::Device& device,
        ID3D12RootSignature* root_signature,
        const wz::rhi::RenderProgramDesc& program,
        const wz::rhi::ProgramBytecode& bytecode)
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
        if (program.blend_mode == wz::rhi::BlendMode::AlphaBlend) {
            D3D12_RENDER_TARGET_BLEND_DESC& rt =
                desc.BlendState.RenderTarget[0];
            rt.BlendEnable = TRUE;
            rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            rt.BlendOp = D3D12_BLEND_OP_ADD;
            rt.SrcBlendAlpha = D3D12_BLEND_ONE;
            rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }

        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
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
        return SUCCEEDED(hr) ? pso : nullptr;
    }
}

namespace wz::engine::rendering
{
    std::optional<uint32_t> RhiDx12PipelineLayout::root_param_for_slot(
        uint32_t binding_slot) const noexcept
    {
        for (const RhiDx12DescriptorTableParam& table : descriptor_tables) {
            if (table.binding_slot == binding_slot) {
                return table.root_parameter_index;
            }
        }
        return std::nullopt;
    }

    std::optional<RhiDx12PipelineLayout> plan_dx12_pipeline_layout(
        const wz::rhi::RenderProgramDesc& program)
    {
        if (!wz::rhi::shader_resource_group_slots_are_unique(
                program.shader_resource_groups))
        {
            return std::nullopt;
        }

        std::vector<const wz::rhi::ShaderResourceGroupLayout*> srgs;
        srgs.reserve(program.shader_resource_groups.size());
        for (const wz::rhi::ShaderResourceGroupLayout& srg
             : program.shader_resource_groups)
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
                    descriptor_count });
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

        return layout;
    }

    RhiDx12PipelineCache::RhiDx12PipelineCache(
        wz::gpu::Device& device,
        const wz::rhi::RenderProgramRegistry& programs,
        const wz::rhi::ShaderModuleRegistry& shaders)
        : device_(&device)
        , programs_(&programs)
        , shaders_(&shaders)
    {
    }

    RhiDx12PipelineCache::~RhiDx12PipelineCache()
    {
        clear();
    }

    const RhiDx12RealizedPipeline* RhiDx12PipelineCache::get(
        wz::rhi::Tag program) const noexcept
    {
        for (const Entry& entry : entries_) {
            if (entry.program == program) {
                return &entry.realized;
            }
        }
        return nullptr;
    }

    const RhiDx12RealizedPipeline* RhiDx12PipelineCache::realize(
        wz::rhi::Tag program)
    {
        if (const RhiDx12RealizedPipeline* existing = get(program)) {
            return existing;
        }
        if (!device_ || !programs_ || !shaders_ || !program.valid()) {
            return nullptr;
        }

        const wz::rhi::RenderProgramDesc* desc = programs_->get(program);
        if (!desc) {
            return nullptr;
        }

        const std::optional<wz::rhi::ProgramBytecode> bytecode =
            wz::rhi::resolve_program_bytecode(*desc, *shaders_);
        if (!bytecode) {
            return nullptr;
        }

        const std::optional<RhiDx12PipelineLayout> layout =
            plan_dx12_pipeline_layout(*desc);
        if (!layout) {
            return nullptr;
        }

        ID3D12Device* d3d = wz::gpu::dx12::internal::get_device(*device_);
        ID3D12RootSignature* root_signature =
            create_root_signature(d3d, *desc, *layout);
        if (!root_signature) {
            return nullptr;
        }

        ID3D12PipelineState* pso = create_pipeline_state(
            *device_,
            root_signature,
            *desc,
            *bytecode);
        if (!pso) {
            root_signature->Release();
            return nullptr;
        }

        entries_.push_back(Entry{
            program,
            RhiDx12RealizedPipeline{
                root_signature,
                pso,
                *layout,
                static_cast<uint32_t>(primitive_topology(desc->topology)) } });
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
