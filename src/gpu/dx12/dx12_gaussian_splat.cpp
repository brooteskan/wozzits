// src/gpu/dx12/dx12_gaussian_splat.cpp

#include <gpu/dx12/dx12_internal.h>

#include "dx12_device_internal.h"

#include <engine/assets/gaussian_splat/gaussian_splat.h>

#include <cassert>
#include <cstring>
#include <vector>

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

            if (!device || !src || byte_count == 0)
                return false;

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

            if (FAILED(hr))
                return false;

            void* mapped = nullptr;

            const D3D12_RANGE read_range{
                0,
                0,
            };

            const HRESULT map_hr = resource->Map(
                0,
                &read_range,
                &mapped);

            if (FAILED(map_hr) || !mapped) {
                resource->Release();
                return false;
            }

            std::memcpy(mapped, src, static_cast<size_t>(byte_count));

            resource->Unmap(
                0,
                nullptr);

            *out_resource = resource;
            return true;
        }

        DX12GaussianSplatVertex make_gpu_splat_vertex(
            const wz::engine::assets::GaussianSplat& splat)
        {
            DX12GaussianSplatVertex out{};

            out.position[0] = splat.position[0];
            out.position[1] = splat.position[1];
            out.position[2] = splat.position[2];

            // V1 debug path treats splat scale as a simple scalar.
            // Use the largest axis so non-uniform splats remain visible.
            out.scale = splat.scale[0];
            if (splat.scale[1] > out.scale)
                out.scale = splat.scale[1];
            if (splat.scale[2] > out.scale)
                out.scale = splat.scale[2];

            out.color[0] = splat.color[0];
            out.color[1] = splat.color[1];
            out.color[2] = splat.color[2];

            out.opacity = splat.opacity;

            return out;
        }
    }

    DX12GaussianSplatCloudTable::DX12GaussianSplatCloudTable()
    {
        slots_.push_back({});
    }

    GPUHandle DX12GaussianSplatCloudTable::add(
        DX12GaussianSplatCloudResource cloud)
    {
        if (!cloud.vertex_buffer || cloud.splat_count == 0)
            return {};

        const uint32_t id = static_cast<uint32_t>(slots_.size());

        Slot slot{};
        slot.epoch = 1;
        slot.occupied = true;
        slot.cloud = cloud;

        slots_.push_back(slot);

        return GPUHandle{
            .id = id,
            .epoch = slot.epoch,
            .type = GPUResourceType::GaussianSplatCloud,
        };
    }

    const DX12GaussianSplatCloudResource* DX12GaussianSplatCloudTable::get(
        GPUHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        if (handle.type != GPUResourceType::GaussianSplatCloud)
            return nullptr;

        if (handle.id >= slots_.size())
            return nullptr;

        const Slot& slot = slots_[handle.id];

        if (!slot.occupied)
            return nullptr;

        if (slot.epoch != handle.epoch)
            return nullptr;

        return &slot.cloud;
    }

    void DX12GaussianSplatCloudTable::destroy()
    {
        for (Slot& slot : slots_) {
            if (slot.cloud.vertex_buffer) {
                slot.cloud.vertex_buffer->Release();
                slot.cloud.vertex_buffer = nullptr;
            }

            slot.occupied = false;
            slot.epoch = 0;
            slot.cloud = {};
        }

        slots_.clear();
        slots_.push_back({});
    }

    GPUHandle upload_gaussian_splat_cloud_dx12(
        Device& device,
        const wz::engine::assets::GaussianSplatCloudData& cloud)
    {
        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(device.impl);
        assert(impl);

        if (!cloud.valid())
            return {};

        if (!impl->device)
            return {};

        if (cloud.splats.empty())
            return {};

        std::vector<DX12GaussianSplatVertex> vertices;
        vertices.reserve(cloud.splats.size());

        for (const wz::engine::assets::GaussianSplat& splat : cloud.splats) {
            vertices.push_back(make_gpu_splat_vertex(splat));
        }

        const uint64_t vertex_bytes =
            static_cast<uint64_t>(vertices.size()) *
            static_cast<uint64_t>(sizeof(DX12GaussianSplatVertex));

        DX12GaussianSplatCloudResource resource{};
        resource.splat_count = static_cast<uint32_t>(vertices.size());

        if (!create_upload_buffer(
            impl->device,
            vertices.data(),
            vertex_bytes,
            &resource.vertex_buffer))
        {
            return {};
        }

        resource.vertex_view.BufferLocation =
            resource.vertex_buffer->GetGPUVirtualAddress();
        resource.vertex_view.SizeInBytes =
            static_cast<UINT>(vertex_bytes);
        resource.vertex_view.StrideInBytes =
            static_cast<UINT>(sizeof(DX12GaussianSplatVertex));

        return impl->gaussian_splat_clouds.add(resource);
    }

    const DX12GaussianSplatCloudResource* get_gaussian_splat_cloud(
        Device& device,
        GPUHandle handle)
    {
        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(device.impl);
        assert(impl);

        return impl->gaussian_splat_clouds.get(handle);
    }
}