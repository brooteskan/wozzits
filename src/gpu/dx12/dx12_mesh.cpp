// src/gpu/dx12/dx12_mesh.cpp

#include <gpu/dx12/dx12_internal.h>

#include "dx12_device_internal.h"

#include <engine/assets/mesh/mesh.h>

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
                IID_PPV_ARGS(&resource)
            );

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
                &mapped
            );

            if (FAILED(map_hr) || !mapped) {
                resource->Release();
                return false;
            }

            std::memcpy(mapped, src, static_cast<size_t>(byte_count));

            resource->Unmap(
                0,
                nullptr
            );

            *out_resource = resource;
            return true;
        }
    }

    GPUHandle upload_mesh_dx12(
        Device& device,
        const wz::engine::assets::MeshData& mesh)
    {
        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(device.impl);
        assert(impl);

        if (!mesh.valid())
            return {};

        if (!impl->device)
            return {};

        if (mesh.vertices.empty() || mesh.indices.empty())
            return {};

        const uint64_t vertex_bytes =
            static_cast<uint64_t>(mesh.vertices.size()) *
            static_cast<uint64_t>(sizeof(wz::engine::assets::MeshVertex));

        const uint64_t index_bytes =
            static_cast<uint64_t>(mesh.indices.size()) *
            static_cast<uint64_t>(sizeof(uint32_t));

        DX12MeshResource resource{};
        resource.vertex_count = mesh.vertex_count();
        resource.index_count = mesh.index_count();

        if (!create_upload_buffer(
            impl->device,
            mesh.vertices.data(),
            vertex_bytes,
            &resource.vertex_buffer))
        {
            return {};
        }

        if (!create_upload_buffer(
            impl->device,
            mesh.indices.data(),
            index_bytes,
            &resource.index_buffer))
        {
            if (resource.vertex_buffer) {
                resource.vertex_buffer->Release();
                resource.vertex_buffer = nullptr;
            }

            return {};
        }

        resource.vertex_view.BufferLocation =
            resource.vertex_buffer->GetGPUVirtualAddress();
        resource.vertex_view.SizeInBytes =
            static_cast<UINT>(vertex_bytes);
        resource.vertex_view.StrideInBytes =
            static_cast<UINT>(sizeof(wz::engine::assets::MeshVertex));

        resource.index_view.BufferLocation =
            resource.index_buffer->GetGPUVirtualAddress();
        resource.index_view.SizeInBytes =
            static_cast<UINT>(index_bytes);
        resource.index_view.Format = DXGI_FORMAT_R32_UINT;

        return impl->meshes.add(resource);
    }

    const DX12MeshResource* get_mesh(
        Device& device,
        GPUHandle handle)
    {
        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(device.impl);
        assert(impl);

        return impl->meshes.get(handle);
    }
}