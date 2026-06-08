#include <gpu/dx12/dx12_internal.h>

#include "dx12_device_internal.h"

#include <engine/assets/type_extensions.h>
#include <gpu/mesh_field_visualization.h>

#include <algorithm>
#include <cassert>
#include <cstring>

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

        const wz::engine::assets::MeshDerivedFieldChannel* find_float1_channel(
            const wz::engine::assets::MeshDerivedFieldData& field,
            uint32_t channel_id)
        {
            const auto found = std::find_if(
                field.channels.begin(),
                field.channels.end(),
                [channel_id](
                    const wz::engine::assets::MeshDerivedFieldChannel& channel)
                {
                    return channel.channel_id == channel_id;
                });
            if (found == field.channels.end()
                || found->value_type
                    != wz::engine::assets::MeshDerivedFieldValueType::Float1)
            {
                return nullptr;
            }
            return &*found;
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
                .type = wz::engine::assets::kAssetTypeGPUMeshFieldBuffer,
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
            .type = wz::engine::assets::kAssetTypeGPUMeshFieldBuffer,
        };
    }

    const DX12MeshFieldVisualizationResource*
    DX12MeshFieldVisualizationTable::get(GPUHandle handle) const
    {
        if (!handle.valid()
            || handle.type
                != wz::engine::assets::kAssetTypeGPUMeshFieldBuffer
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
            || handle.type
                != wz::engine::assets::kAssetTypeGPUMeshFieldBuffer
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

        const auto* channel = find_float1_channel(
            *desc.field,
            desc.channel_id);
        if (!channel) {
            return {};
        }

        const auto* value_bytes =
            desc.field->values.data() + channel->byte_offset;

        DX12MeshFieldVisualizationResource resource{};
        resource.element_count = desc.field->element_count;

        if (!create_upload_buffer(
                impl->device,
                value_bytes,
                channel->byte_count,
                &resource.values_buffer))
        {
            return {};
        }

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
            sizeof(float));

        return impl->mesh_field_visualizations.add(resource);
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
