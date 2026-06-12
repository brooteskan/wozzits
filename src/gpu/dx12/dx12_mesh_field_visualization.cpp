#include <gpu/dx12/dx12_internal.h>

#include "dx12_device_internal.h"

#include <gpu/gpu_resource_types.h>
#include <gpu/mesh_field_visualization.h>

#include <cassert>
#include <cstring>
#include <limits>

namespace wz::gpu::dx12::internal
{
    namespace
    {
        bool create_upload_buffer(
            ID3D12Device* device,
            const void* src,
            uint64_t byte_count,
            ID3D12Resource** out_resource)
        {
            assert(out_resource);
            *out_resource = nullptr;

            if (!device || !src || byte_count == 0u) {
                return false;
            }

            const D3D12_HEAP_PROPERTIES heap_props =
                CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            const D3D12_RESOURCE_DESC resource_desc =
                CD3DX12_RESOURCE_DESC::Buffer(byte_count);

            ID3D12Resource* resource = nullptr;
            const HRESULT hr = device->CreateCommittedResource(
                &heap_props,
                D3D12_HEAP_FLAG_NONE,
                &resource_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&resource));

            if (FAILED(hr)) {
                return false;
            }

            void* mapped = nullptr;
            const D3D12_RANGE read_range{ 0, 0 };
            const HRESULT map_hr = resource->Map(0, &read_range, &mapped);
            if (FAILED(map_hr) || !mapped) {
                resource->Release();
                return false;
            }

            std::memcpy(mapped, src, static_cast<size_t>(byte_count));
            resource->Unmap(0, nullptr);

            *out_resource = resource;
            return true;
        }

        void release_mesh_field_visualization_resource(
            DX12MeshFieldVisualizationResource& resource)
        {
            if (resource.values_buffer) {
                resource.values_buffer->Release();
                resource.values_buffer = nullptr;
            }
            resource.element_count = 0;
            resource.srv_table = {};
        }

        void transition_resource(
            ID3D12GraphicsCommandList* cmd,
            ID3D12Resource* resource,
            D3D12_RESOURCE_STATES before,
            D3D12_RESOURCE_STATES after)
        {
            if (!cmd || !resource || before == after) {
                return;
            }

            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resource;
            barrier.Transition.StateBefore = before;
            barrier.Transition.StateAfter = after;
            barrier.Transition.Subresource =
                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmd->ResourceBarrier(1, &barrier);
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
            if (!dx12_check_hr(*impl, hr, "ID3D12GraphicsCommandList::Close")) {
                return false;
            }

            ID3D12CommandList* lists[] = { cmd };
            impl->queue->ExecuteCommandLists(1, lists);

            const UINT64 fence_value = impl->fence_value;
            hr = impl->queue->Signal(impl->fence, fence_value);
            if (!dx12_check_hr(*impl, hr, "ID3D12CommandQueue::Signal")) {
                return false;
            }

            if (impl->fence->GetCompletedValue() < fence_value) {
                hr = impl->fence->SetEventOnCompletion(
                    fence_value,
                    impl->fence_event);
                if (!dx12_check_hr(
                        *impl,
                        hr,
                        "ID3D12Fence::SetEventOnCompletion")) {
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
            if (!dx12_check_hr(
                    *impl,
                    hr,
                    "ID3D12Device::CreateCommandAllocator")) {
                return false;
            }

            hr = impl->device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                *out_allocator,
                nullptr,
                IID_PPV_ARGS(out_cmd));
            if (!dx12_check_hr(
                    *impl,
                    hr,
                    "ID3D12Device::CreateCommandList")) {
                (*out_allocator)->Release();
                *out_allocator = nullptr;
                return false;
            }

            return true;
        }
    }

    DX12MeshFieldVisualizationTable::DX12MeshFieldVisualizationTable()
    {
        slots_.emplace_back();
    }

    GPUHandle DX12MeshFieldVisualizationTable::add(
        DX12MeshFieldVisualizationResource resource)
    {
        if (!resource.valid()) {
            return {};
        }

        for (uint32_t id = 1; id < static_cast<uint32_t>(slots_.size()); ++id)
        {
            Slot& slot = slots_[id];
            if (slot.occupied) {
                continue;
            }

            if (slot.epoch == 0u) {
                slot.epoch = 1u;
            }
            slot.occupied = true;
            slot.resource = resource;
            return GPUHandle{
                .id = id,
                .epoch = slot.epoch,
                .type = kGPUMeshFieldBufferResourceType,
            };
        }

        Slot slot{};
        slot.epoch = 1u;
        slot.occupied = true;
        slot.resource = resource;
        slots_.push_back(slot);

        return GPUHandle{
            .id = static_cast<uint32_t>(slots_.size() - 1u),
            .epoch = slot.epoch,
            .type = kGPUMeshFieldBufferResourceType,
        };
    }

    const DX12MeshFieldVisualizationResource*
    DX12MeshFieldVisualizationTable::get(GPUHandle handle) const
    {
        if (!handle.valid()
            || handle.type != kGPUMeshFieldBufferResourceType
            || handle.id == 0u
            || handle.id >= slots_.size())
        {
            return nullptr;
        }

        const Slot& slot = slots_[handle.id];
        if (!slot.occupied || slot.epoch != handle.epoch) {
            return nullptr;
        }
        return &slot.resource;
    }

    bool DX12MeshFieldVisualizationTable::release(GPUHandle handle)
    {
        if (!handle.valid()
            || handle.type != kGPUMeshFieldBufferResourceType
            || handle.id == 0u
            || handle.id >= slots_.size())
        {
            return false;
        }

        Slot& slot = slots_[handle.id];
        if (!slot.occupied || slot.epoch != handle.epoch) {
            return false;
        }

        release_mesh_field_visualization_resource(slot.resource);
        slot.occupied = false;
        ++slot.epoch;
        if (slot.epoch == 0u) {
            slot.epoch = 1u;
        }
        return true;
    }

    void DX12MeshFieldVisualizationTable::destroy()
    {
        for (Slot& slot : slots_) {
            if (!slot.occupied) {
                continue;
            }
            release_mesh_field_visualization_resource(slot.resource);
            slot.occupied = false;
            ++slot.epoch;
        }
        slots_.clear();
        slots_.emplace_back();
    }

    GPUHandle upload_mesh_field_visualization_dx12(
        Device& device,
        const wz::gpu::MeshFieldVisualizationUploadDesc& desc)
    {
        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(device.impl);
        assert(impl);

        if (!impl || !impl->device || !desc.valid()) {
            return {};
        }

        const std::byte* value_bytes = desc.values_begin();
        const uint64_t value_byte_count = desc.value_byte_count();
        const uint32_t element_count = desc.element_count();
        const uint32_t stride_bytes = desc.stride_bytes();
        if (!value_bytes
            || value_byte_count == 0u
            || element_count == 0u
            || stride_bytes == 0u)
        {
            return {};
        }

        // Stage the CPU data and copy it into a DEFAULT-heap buffer so the
        // resulting resource can later be refreshed in place from a GPU
        // source (behavior compute publish path).
        ID3D12Resource* staging = nullptr;
        if (!create_upload_buffer(
                impl->device,
                value_bytes,
                value_byte_count,
                &staging))
        {
            return {};
        }

        const D3D12_HEAP_PROPERTIES default_heap =
            CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const D3D12_RESOURCE_DESC buffer_desc =
            CD3DX12_RESOURCE_DESC::Buffer(value_byte_count);

        ID3D12Resource* destination = nullptr;
        HRESULT hr = impl->device->CreateCommittedResource(
            &default_heap,
            D3D12_HEAP_FLAG_NONE,
            &buffer_desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&destination));
        if (FAILED(hr)) {
            staging->Release();
            return {};
        }

        ID3D12CommandAllocator* allocator = nullptr;
        ID3D12GraphicsCommandList* cmd = nullptr;
        if (!create_one_shot_command_list(impl, &allocator, &cmd)) {
            destination->Release();
            staging->Release();
            return {};
        }

        cmd->CopyBufferRegion(
            destination,
            0,
            staging,
            0,
            value_byte_count);
        transition_resource(
            cmd,
            destination,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
                | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        const bool copied = execute_and_wait(impl, allocator, cmd);
        cmd->Release();
        allocator->Release();
        staging->Release();
        if (!copied) {
            destination->Release();
            return {};
        }

        DX12MeshFieldVisualizationResource resource{};
        resource.values_buffer = destination;
        resource.element_count = element_count;
        resource.stride_bytes = stride_bytes;
        resource.gpu_updatable = true;

        resource.srv_table = impl->srv_cbv_uav_allocator.allocate(1);
        if (!resource.srv_table.valid()) {
            release_mesh_field_visualization_resource(resource);
            return {};
        }

        impl->srv_cbv_uav_allocator.create_structured_buffer_srv(
            resource.srv_table,
            0,
            resource.values_buffer,
            resource.element_count,
            resource.stride_bytes);

        return impl->mesh_field_visualizations.add(resource);
    }

    GPUHandle create_mesh_field_visualization_from_gpu_source_dx12(
        Device& device,
        GPUHandle source_buffer,
        uint64_t byte_offset,
        uint32_t element_count,
        uint32_t stride_bytes)
    {
        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(device.impl);
        if (!impl
            || !impl->device
            || !source_buffer.valid()
            || element_count == 0u
            || stride_bytes == 0u)
        {
            return {};
        }

        DX12ComputeBuffer* source =
            impl->compute_buffers.get(source_buffer);
        if (!source || !source->valid()) {
            return {};
        }

        const uint64_t byte_count =
            static_cast<uint64_t>(element_count)
            * static_cast<uint64_t>(stride_bytes);
        const uint64_t source_byte_count =
            static_cast<uint64_t>(source->element_count)
            * static_cast<uint64_t>(source->stride_bytes);
        if (byte_count == 0u
            || byte_offset > source_byte_count
            || byte_count > source_byte_count - byte_offset
            || byte_count > (std::numeric_limits<size_t>::max)())
        {
            return {};
        }

        const D3D12_HEAP_PROPERTIES default_heap =
            CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const D3D12_RESOURCE_DESC buffer_desc =
            CD3DX12_RESOURCE_DESC::Buffer(byte_count);

        ID3D12Resource* destination = nullptr;
        HRESULT hr = impl->device->CreateCommittedResource(
            &default_heap,
            D3D12_HEAP_FLAG_NONE,
            &buffer_desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&destination));
        if (FAILED(hr)) {
            return {};
        }

        ID3D12CommandAllocator* allocator = nullptr;
        ID3D12GraphicsCommandList* cmd = nullptr;
        if (!create_one_shot_command_list(impl, &allocator, &cmd)) {
            destination->Release();
            return {};
        }

        const D3D12_RESOURCE_STATES before = source->state;
        transition_resource(
            cmd,
            source->resource,
            before,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmd->CopyBufferRegion(
            destination,
            0,
            source->resource,
            byte_offset,
            byte_count);
        transition_resource(
            cmd,
            destination,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
                | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        transition_resource(
            cmd,
            source->resource,
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            before);
        source->state = before;

        const bool copied = execute_and_wait(impl, allocator, cmd);
        cmd->Release();
        allocator->Release();
        if (!copied) {
            destination->Release();
            return {};
        }

        DX12MeshFieldVisualizationResource resource{};
        resource.values_buffer = destination;
        resource.element_count = element_count;
        resource.stride_bytes = stride_bytes;
        resource.gpu_updatable = true;
        resource.srv_table = impl->srv_cbv_uav_allocator.allocate(1);
        if (!resource.srv_table.valid()) {
            release_mesh_field_visualization_resource(resource);
            return {};
        }

        impl->srv_cbv_uav_allocator.create_structured_buffer_srv(
            resource.srv_table,
            0,
            resource.values_buffer,
            resource.element_count,
            stride_bytes);

        return impl->mesh_field_visualizations.add(resource);
    }

    bool update_mesh_field_visualization_from_gpu_source_dx12(
        Device& device,
        GPUHandle destination,
        GPUHandle source_buffer,
        uint64_t byte_offset,
        uint32_t element_count,
        uint32_t stride_bytes)
    {
        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(device.impl);
        if (!impl
            || !impl->device
            || !destination.valid()
            || !source_buffer.valid()
            || element_count == 0u
            || stride_bytes == 0u)
        {
            return false;
        }

        const DX12MeshFieldVisualizationResource* dest =
            impl->mesh_field_visualizations.get(destination);
        if (!dest
            || !dest->valid()
            || !dest->gpu_updatable
            || dest->element_count != element_count
            || dest->stride_bytes != stride_bytes)
        {
            return false;
        }

        DX12ComputeBuffer* source =
            impl->compute_buffers.get(source_buffer);
        if (!source || !source->valid()) {
            return false;
        }

        const uint64_t byte_count =
            static_cast<uint64_t>(element_count)
            * static_cast<uint64_t>(stride_bytes);
        const uint64_t source_byte_count =
            static_cast<uint64_t>(source->element_count)
            * static_cast<uint64_t>(source->stride_bytes);
        if (byte_count == 0u
            || byte_offset > source_byte_count
            || byte_count > source_byte_count - byte_offset)
        {
            return false;
        }

        ID3D12CommandAllocator* allocator = nullptr;
        ID3D12GraphicsCommandList* cmd = nullptr;
        if (!create_one_shot_command_list(impl, &allocator, &cmd)) {
            return false;
        }

        const D3D12_RESOURCE_STATES dest_resident_state =
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
            | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        const D3D12_RESOURCE_STATES source_before = source->state;
        transition_resource(
            cmd,
            dest->values_buffer,
            dest_resident_state,
            D3D12_RESOURCE_STATE_COPY_DEST);
        transition_resource(
            cmd,
            source->resource,
            source_before,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmd->CopyBufferRegion(
            dest->values_buffer,
            0,
            source->resource,
            byte_offset,
            byte_count);
        transition_resource(
            cmd,
            dest->values_buffer,
            D3D12_RESOURCE_STATE_COPY_DEST,
            dest_resident_state);
        transition_resource(
            cmd,
            source->resource,
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            source_before);
        source->state = source_before;

        const bool copied = execute_and_wait(impl, allocator, cmd);
        cmd->Release();
        allocator->Release();
        return copied;
    }

    const DX12MeshFieldVisualizationResource*
    get_mesh_field_visualization(Device& device, GPUHandle handle)
    {
        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(device.impl);
        return impl ? impl->mesh_field_visualizations.get(handle) : nullptr;
    }

    bool release_mesh_field_visualization_dx12(
        Device& device,
        GPUHandle handle)
    {
        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(device.impl);
        return impl ? impl->mesh_field_visualizations.release(handle) : false;
    }
}
