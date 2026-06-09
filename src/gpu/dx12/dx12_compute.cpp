#include <gpu/compute.h>
#include <gpu/dx12/dx12_internal.h>
#include <gpu/gpu_resource_types.h>

#include "dx12_device_internal.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <span>
#include <utility>

namespace wz::gpu::dx12::internal
{
    namespace
    {
        using ComputeBindingKind = wz::gpu::ComputeBindingKind;

        uint64_t buffer_byte_count(const DX12ComputeBuffer& buffer) noexcept
        {
            return static_cast<uint64_t>(buffer.element_count)
                * static_cast<uint64_t>(buffer.stride_bytes);
        }

        void release_compute_buffer_resource(DX12ComputeBuffer& buffer)
        {
            if (buffer.resource) {
                buffer.resource->Release();
                buffer.resource = nullptr;
            }
            buffer.element_count = 0;
            buffer.stride_bytes = 0;
            buffer.state = D3D12_RESOURCE_STATE_COMMON;
        }

        void release_compute_pipeline_resource(DX12ComputePipeline& pipeline)
        {
            if (pipeline.pso) {
                pipeline.pso->Release();
                pipeline.pso = nullptr;
            }
            if (pipeline.root_sig) {
                pipeline.root_sig->Release();
                pipeline.root_sig = nullptr;
            }
            pipeline.data = {};
        }

        bool is_srv(ComputeBindingKind kind) noexcept
        {
            return kind == ComputeBindingKind::StructuredBufferSRV
                || kind == ComputeBindingKind::ByteAddressBufferSRV;
        }

        D3D12_RESOURCE_STATES desired_state(ComputeBindingKind kind) noexcept
        {
            return is_srv(kind)
                ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
                : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }

        D3D12_ROOT_PARAMETER_TYPE root_parameter_type(
            ComputeBindingKind kind) noexcept
        {
            return is_srv(kind)
                ? D3D12_ROOT_PARAMETER_TYPE_SRV
                : D3D12_ROOT_PARAMETER_TYPE_UAV;
        }

        ComputeBindingKind gpu_binding_kind(
            wz::engine::assets::ComputeBindingKind kind) noexcept
        {
            using AssetKind = wz::engine::assets::ComputeBindingKind;

            switch (kind) {
            case AssetKind::StructuredBufferSRV:
                return ComputeBindingKind::StructuredBufferSRV;
            case AssetKind::StructuredBufferUAV:
                return ComputeBindingKind::StructuredBufferUAV;
            case AssetKind::ByteAddressBufferSRV:
                return ComputeBindingKind::ByteAddressBufferSRV;
            case AssetKind::ByteAddressBufferUAV:
                return ComputeBindingKind::ByteAddressBufferUAV;
            }
            return ComputeBindingKind::StructuredBufferSRV;
        }

        bool execute_and_wait(
            DX12Device* impl,
            ID3D12CommandAllocator* allocator,
            ID3D12GraphicsCommandList* cmd)
        {
            if (!impl || !allocator || !cmd) {
                return false;
            }

            HRESULT hr = cmd->Close();
            if (FAILED(hr)) {
                return false;
            }

            ID3D12CommandList* lists[] = { cmd };
            impl->queue->ExecuteCommandLists(1, lists);

            const UINT64 fence_value = impl->fence_value;
            hr = impl->queue->Signal(impl->fence, fence_value);
            if (FAILED(hr)) {
                return false;
            }

            if (impl->fence->GetCompletedValue() < fence_value) {
                hr = impl->fence->SetEventOnCompletion(
                    fence_value,
                    impl->fence_event);
                if (FAILED(hr)) {
                    return false;
                }
                WaitForSingleObject(impl->fence_event, INFINITE);
            }

            ++impl->fence_value;
            return true;
        }

        bool create_one_shot_command_list(
            DX12Device* impl,
            ID3D12CommandAllocator** out_allocator,
            ID3D12GraphicsCommandList** out_cmd)
        {
            assert(out_allocator);
            assert(out_cmd);
            *out_allocator = nullptr;
            *out_cmd = nullptr;

            if (!impl || !impl->device) {
                return false;
            }

            HRESULT hr = impl->device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(out_allocator));
            if (FAILED(hr)) {
                return false;
            }

            hr = impl->device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                *out_allocator,
                nullptr,
                IID_PPV_ARGS(out_cmd));
            if (FAILED(hr)) {
                (*out_allocator)->Release();
                *out_allocator = nullptr;
                return false;
            }

            return true;
        }

        bool transition_buffer(
            ID3D12GraphicsCommandList* cmd,
            DX12ComputeBuffer& buffer,
            D3D12_RESOURCE_STATES after)
        {
            if (!cmd || !buffer.valid()) {
                return false;
            }

            if (buffer.state == after) {
                return true;
            }

            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = buffer.resource;
            barrier.Transition.StateBefore = buffer.state;
            barrier.Transition.StateAfter = after;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmd->ResourceBarrier(1, &barrier);
            buffer.state = after;
            return true;
        }

        const wz::engine::assets::ComputeBindingDesc* find_pipeline_binding(
            const wz::engine::assets::ComputePipelineData& data,
            const wz::gpu::ComputeDispatchBinding& binding)
        {
            const auto found = std::find_if(
                data.bindings.begin(),
                data.bindings.end(),
                [&binding](const wz::engine::assets::ComputeBindingDesc& declared)
                {
                    return gpu_binding_kind(declared.kind) == binding.kind
                        && declared.shader_register == binding.shader_register
                        && declared.register_space == binding.register_space;
                });
            return found == data.bindings.end() ? nullptr : &*found;
        }

        uint32_t root_param_index_for_binding(
            const wz::engine::assets::ComputePipelineData& data,
            const wz::engine::assets::ComputeBindingDesc* binding)
        {
            const uint32_t base = data.root_constant_dwords > 0u ? 1u : 0u;
            for (uint32_t i = 0; i < data.bindings.size(); ++i) {
                if (&data.bindings[i] == binding) {
                    return base + i;
                }
            }
            return UINT32_MAX;
        }

        ID3D12RootSignature* create_compute_root_signature(
            ID3D12Device* device,
            const wz::engine::assets::ComputePipelineData& data)
        {
            if (!device || !data.valid()) {
                return nullptr;
            }

            std::vector<D3D12_ROOT_PARAMETER> params;
            params.reserve(
                data.bindings.size()
                + (data.root_constant_dwords > 0u ? 1u : 0u));

            if (data.root_constant_dwords > 0u) {
                D3D12_ROOT_PARAMETER param{};
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                param.Constants.Num32BitValues = data.root_constant_dwords;
                param.Constants.ShaderRegister = 0;
                param.Constants.RegisterSpace = 0;
                params.push_back(param);
            }

            for (const auto& binding : data.bindings) {
                D3D12_ROOT_PARAMETER param{};
                param.ParameterType =
                    root_parameter_type(gpu_binding_kind(binding.kind));
                param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                param.Descriptor.ShaderRegister = binding.shader_register;
                param.Descriptor.RegisterSpace = binding.register_space;
                params.push_back(param);
            }

            D3D12_ROOT_SIGNATURE_DESC desc{};
            desc.NumParameters = static_cast<UINT>(params.size());
            desc.pParameters = params.empty() ? nullptr : params.data();
            desc.NumStaticSamplers = 0;
            desc.pStaticSamplers = nullptr;
            desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

            ID3DBlob* sig_blob = nullptr;
            ID3DBlob* error_blob = nullptr;
            HRESULT hr = D3D12SerializeRootSignature(
                &desc,
                D3D_ROOT_SIGNATURE_VERSION_1,
                &sig_blob,
                &error_blob);
            if (FAILED(hr)) {
                if (error_blob) {
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
            if (error_blob) {
                error_blob->Release();
            }

            return SUCCEEDED(hr) ? root_sig : nullptr;
        }

        ID3D12PipelineState* create_compute_pso(
            Device& device,
            ID3D12RootSignature* root_sig,
            GPUHandle compute_shader)
        {
            const DX12Shader* cs = get_shader(device, compute_shader);
            if (!root_sig
                || !cs
                || !cs->blob
                || cs->stage != ShaderStage::Compute)
            {
                return nullptr;
            }

            D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = root_sig;
            desc.CS = {
                cs->blob->GetBufferPointer(),
                cs->blob->GetBufferSize(),
            };

            ID3D12PipelineState* pso = nullptr;
            const HRESULT hr = get_device(device)->CreateComputePipelineState(
                &desc,
                IID_PPV_ARGS(&pso));
            return SUCCEEDED(hr) ? pso : nullptr;
        }
    }

    DX12ComputeBufferTable::DX12ComputeBufferTable()
    {
        slots_.emplace_back();
    }

    GPUHandle DX12ComputeBufferTable::add(DX12ComputeBuffer buffer)
    {
        if (!buffer.valid()) {
            return {};
        }

        for (uint32_t id = 1; id < static_cast<uint32_t>(slots_.size()); ++id) {
            Slot& slot = slots_[id];
            if (slot.occupied) {
                continue;
            }

            if (slot.epoch == 0u) {
                slot.epoch = 1u;
            }
            slot.occupied = true;
            slot.buffer = buffer;
            return GPUHandle{
                .id = id,
                .epoch = slot.epoch,
                .type = kGPUBufferResourceType,
            };
        }

        Slot slot{};
        slot.epoch = 1u;
        slot.occupied = true;
        slot.buffer = buffer;
        slots_.push_back(slot);

        return GPUHandle{
            .id = static_cast<uint32_t>(slots_.size() - 1u),
            .epoch = slot.epoch,
            .type = kGPUBufferResourceType,
        };
    }

    DX12ComputeBuffer* DX12ComputeBufferTable::get(GPUHandle handle)
    {
        return const_cast<DX12ComputeBuffer*>(
            static_cast<const DX12ComputeBufferTable*>(this)->get(handle));
    }

    const DX12ComputeBuffer* DX12ComputeBufferTable::get(
        GPUHandle handle) const
    {
        if (!handle.valid()
            || handle.type != kGPUBufferResourceType
            || handle.id == 0u
            || handle.id >= slots_.size())
        {
            return nullptr;
        }

        const Slot& slot = slots_[handle.id];
        if (!slot.occupied || slot.epoch != handle.epoch) {
            return nullptr;
        }
        return &slot.buffer;
    }

    bool DX12ComputeBufferTable::release(GPUHandle handle)
    {
        if (!handle.valid()
            || handle.type != kGPUBufferResourceType
            || handle.id == 0u
            || handle.id >= slots_.size())
        {
            return false;
        }

        Slot& slot = slots_[handle.id];
        if (!slot.occupied || slot.epoch != handle.epoch) {
            return false;
        }

        release_compute_buffer_resource(slot.buffer);
        slot.occupied = false;
        ++slot.epoch;
        if (slot.epoch == 0u) {
            slot.epoch = 1u;
        }
        return true;
    }

    void DX12ComputeBufferTable::destroy()
    {
        for (Slot& slot : slots_) {
            if (!slot.occupied) {
                continue;
            }
            release_compute_buffer_resource(slot.buffer);
            slot.occupied = false;
            ++slot.epoch;
        }
        slots_.clear();
        slots_.emplace_back();
    }

    DX12ComputePipelineTable::DX12ComputePipelineTable()
    {
        slots_.emplace_back();
    }

    GPUHandle DX12ComputePipelineTable::add(DX12ComputePipeline pipeline)
    {
        if (!pipeline.valid()) {
            return {};
        }

        Slot slot{};
        slot.epoch = 1u;
        slot.occupied = true;
        slot.pipeline = std::move(pipeline);
        slots_.push_back(std::move(slot));

        return GPUHandle{
            .id = static_cast<uint32_t>(slots_.size() - 1u),
            .epoch = 1u,
            .type = kGPUComputePipelineResourceType,
        };
    }

    const DX12ComputePipeline* DX12ComputePipelineTable::get(
        GPUHandle handle) const
    {
        if (!handle.valid()
            || handle.type != kGPUComputePipelineResourceType
            || handle.id == 0u
            || handle.id >= slots_.size())
        {
            return nullptr;
        }

        const Slot& slot = slots_[handle.id];
        if (!slot.occupied || slot.epoch != handle.epoch) {
            return nullptr;
        }
        return &slot.pipeline;
    }

    bool DX12ComputePipelineTable::release(GPUHandle handle)
    {
        if (!handle.valid()
            || handle.type != kGPUComputePipelineResourceType
            || handle.id == 0u
            || handle.id >= slots_.size())
        {
            return false;
        }

        Slot& slot = slots_[handle.id];
        if (!slot.occupied || slot.epoch != handle.epoch) {
            return false;
        }

        release_compute_pipeline_resource(slot.pipeline);
        slot.occupied = false;
        ++slot.epoch;
        if (slot.epoch == 0u) {
            slot.epoch = 1u;
        }
        return true;
    }

    void DX12ComputePipelineTable::destroy()
    {
        for (Slot& slot : slots_) {
            if (!slot.occupied) {
                continue;
            }
            release_compute_pipeline_resource(slot.pipeline);
            slot.occupied = false;
            ++slot.epoch;
        }
        slots_.clear();
        slots_.emplace_back();
    }

    GPUHandle create_structured_buffer_dx12(
        Device& device,
        const wz::gpu::ComputeBufferDesc& desc,
        bool allow_unordered_access)
    {
        auto* impl = static_cast<DX12Device*>(device.impl);
        if (!impl || !impl->device || !desc.valid()) {
            return {};
        }

        const uint64_t byte_count =
            static_cast<uint64_t>(desc.element_count) * desc.stride_bytes;
        if (desc.initial_data
            && desc.initial_data_bytes != 0u
            && desc.initial_data_bytes != byte_count)
        {
            return {};
        }

        const D3D12_HEAP_PROPERTIES default_heap =
            CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_RESOURCE_DESC resource_desc =
            CD3DX12_RESOURCE_DESC::Buffer(byte_count);
        if (allow_unordered_access) {
            resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }

        ID3D12Resource* resource = nullptr;
        const D3D12_RESOURCE_STATES resting_state = allow_unordered_access
            ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
            : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        D3D12_RESOURCE_STATES initial_state = desc.initial_data
            ? D3D12_RESOURCE_STATE_COPY_DEST
            : resting_state;
        HRESULT hr = impl->device->CreateCommittedResource(
            &default_heap,
            D3D12_HEAP_FLAG_NONE,
            &resource_desc,
            initial_state,
            nullptr,
            IID_PPV_ARGS(&resource));
        if (FAILED(hr)) {
            return {};
        }

        DX12ComputeBuffer buffer{
            .resource = resource,
            .element_count = desc.element_count,
            .stride_bytes = desc.stride_bytes,
            .state = initial_state,
        };

        if (desc.initial_data) {
            const D3D12_HEAP_PROPERTIES upload_heap =
                CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            const D3D12_RESOURCE_DESC upload_desc =
                CD3DX12_RESOURCE_DESC::Buffer(byte_count);
            ID3D12Resource* upload = nullptr;
            hr = impl->device->CreateCommittedResource(
                &upload_heap,
                D3D12_HEAP_FLAG_NONE,
                &upload_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&upload));
            if (FAILED(hr)) {
                resource->Release();
                return {};
            }

            void* mapped = nullptr;
            const D3D12_RANGE read_range{ 0, 0 };
            hr = upload->Map(0, &read_range, &mapped);
            if (FAILED(hr) || !mapped) {
                upload->Release();
                resource->Release();
                return {};
            }
            std::memcpy(mapped, desc.initial_data, static_cast<size_t>(byte_count));
            upload->Unmap(0, nullptr);

            ID3D12CommandAllocator* allocator = nullptr;
            ID3D12GraphicsCommandList* cmd = nullptr;
            if (!create_one_shot_command_list(impl, &allocator, &cmd)) {
                upload->Release();
                resource->Release();
                return {};
            }

            cmd->CopyBufferRegion(resource, 0, upload, 0, byte_count);
            transition_buffer(cmd, buffer, resting_state);
            const bool ok = execute_and_wait(impl, allocator, cmd);

            cmd->Release();
            allocator->Release();
            upload->Release();

            if (!ok) {
                release_compute_buffer_resource(buffer);
                return {};
            }
        }

        return impl->compute_buffers.add(buffer);
    }

    GPUHandle create_compute_pipeline_dx12(
        Device& device,
        const wz::engine::assets::ComputePipelineData& data,
        GPUHandle compute_shader)
    {
        auto* impl = static_cast<DX12Device*>(device.impl);
        if (!impl || !impl->device || !data.valid() || !compute_shader.valid()) {
            return {};
        }

        ID3D12RootSignature* root_sig =
            create_compute_root_signature(impl->device, data);
        if (!root_sig) {
            return {};
        }

        ID3D12PipelineState* pso =
            create_compute_pso(device, root_sig, compute_shader);
        if (!pso) {
            root_sig->Release();
            return {};
        }

        return impl->compute_pipelines.add({
            .root_sig = root_sig,
            .pso = pso,
            .data = data,
        });
    }

    bool dispatch_compute_dx12(
        Device& device,
        const wz::gpu::ComputeDispatchDesc& desc)
    {
        auto* impl = static_cast<DX12Device*>(device.impl);
        if (!impl || !desc.valid()) {
            return false;
        }

        const DX12ComputePipeline* pipeline =
            impl->compute_pipelines.get(desc.pipeline);
        if (!pipeline || !pipeline->valid()) {
            return false;
        }

        if (pipeline->data.root_constant_dwords != desc.root_constants.size()) {
            return false;
        }

        ID3D12CommandAllocator* allocator = nullptr;
        ID3D12GraphicsCommandList* cmd = nullptr;
        if (!create_one_shot_command_list(impl, &allocator, &cmd)) {
            return false;
        }

        cmd->SetComputeRootSignature(pipeline->root_sig);
        cmd->SetPipelineState(pipeline->pso);

        if (pipeline->data.root_constant_dwords > 0u) {
            cmd->SetComputeRoot32BitConstants(
                0,
                pipeline->data.root_constant_dwords,
                desc.root_constants.data(),
                0);
        }

        for (const wz::gpu::ComputeDispatchBinding& binding : desc.bindings) {
            const auto* declared = find_pipeline_binding(pipeline->data, binding);
            if (!declared) {
                cmd->Release();
                allocator->Release();
                return false;
            }

            DX12ComputeBuffer* buffer = impl->compute_buffers.get(binding.buffer);
            if (!buffer || !buffer->valid()) {
                cmd->Release();
                allocator->Release();
                return false;
            }

            if (declared->stride_bytes != buffer->stride_bytes) {
                cmd->Release();
                allocator->Release();
                return false;
            }

            transition_buffer(cmd, *buffer, desired_state(binding.kind));

            const uint32_t root_index =
                root_param_index_for_binding(pipeline->data, declared);
            if (root_index == UINT32_MAX) {
                cmd->Release();
                allocator->Release();
                return false;
            }

            const D3D12_GPU_VIRTUAL_ADDRESS address =
                buffer->resource->GetGPUVirtualAddress();
            if (is_srv(binding.kind)) {
                cmd->SetComputeRootShaderResourceView(root_index, address);
            }
            else {
                cmd->SetComputeRootUnorderedAccessView(root_index, address);
            }
        }

        cmd->Dispatch(
            desc.group_count_x,
            desc.group_count_y,
            desc.group_count_z);

        D3D12_RESOURCE_BARRIER uav_barrier{};
        uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uav_barrier.UAV.pResource = nullptr;
        cmd->ResourceBarrier(1, &uav_barrier);

        const bool ok = execute_and_wait(impl, allocator, cmd);
        cmd->Release();
        allocator->Release();
        return ok;
    }

    std::vector<std::byte> readback_buffer_dx12(
        Device& device,
        GPUHandle handle)
    {
        std::vector<std::byte> out;

        auto* impl = static_cast<DX12Device*>(device.impl);
        if (!impl) {
            return out;
        }

        DX12ComputeBuffer* buffer = impl->compute_buffers.get(handle);
        if (!buffer || !buffer->valid()) {
            return out;
        }

        const uint64_t byte_count = buffer_byte_count(*buffer);
        out.resize(static_cast<size_t>(byte_count));

        const D3D12_HEAP_PROPERTIES readback_heap =
            CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
        const D3D12_RESOURCE_DESC readback_desc =
            CD3DX12_RESOURCE_DESC::Buffer(byte_count);

        ID3D12Resource* readback = nullptr;
        HRESULT hr = impl->device->CreateCommittedResource(
            &readback_heap,
            D3D12_HEAP_FLAG_NONE,
            &readback_desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&readback));
        if (FAILED(hr)) {
            out.clear();
            return out;
        }

        ID3D12CommandAllocator* allocator = nullptr;
        ID3D12GraphicsCommandList* cmd = nullptr;
        if (!create_one_shot_command_list(impl, &allocator, &cmd)) {
            readback->Release();
            out.clear();
            return out;
        }

        transition_buffer(cmd, *buffer, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmd->CopyBufferRegion(readback, 0, buffer->resource, 0, byte_count);
        const bool ok = execute_and_wait(impl, allocator, cmd);

        cmd->Release();
        allocator->Release();

        if (!ok) {
            readback->Release();
            out.clear();
            return out;
        }

        void* mapped = nullptr;
        const D3D12_RANGE read_range{ 0, static_cast<SIZE_T>(byte_count) };
        hr = readback->Map(0, &read_range, &mapped);
        if (FAILED(hr) || !mapped) {
            readback->Release();
            out.clear();
            return out;
        }

        std::memcpy(out.data(), mapped, static_cast<size_t>(byte_count));
        const D3D12_RANGE write_range{ 0, 0 };
        readback->Unmap(0, &write_range);
        readback->Release();
        return out;
    }

    bool release_compute_buffer_dx12(Device& device, GPUHandle handle)
    {
        auto* impl = static_cast<DX12Device*>(device.impl);
        return impl ? impl->compute_buffers.release(handle) : false;
    }

    bool release_compute_pipeline_dx12(Device& device, GPUHandle handle)
    {
        auto* impl = static_cast<DX12Device*>(device.impl);
        return impl ? impl->compute_pipelines.release(handle) : false;
    }
}
