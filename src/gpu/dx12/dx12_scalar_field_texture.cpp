// src/gpu/dx12/dx12_scalar_field_texture.cpp

#include "dx12_device_internal.h"

#include <gpu/dx12/dx12_internal.h>
#include <engine/assets/scalar_field/scalar_field.h>

#include <d3d12.h>
#include <cassert>
#include <cstring>
#include <cstdint>

namespace
{
    bool wait_for_gpu_upload(wz::gpu::dx12::DX12Device* impl)
    {
        HRESULT hr = impl->queue->Signal(impl->fence, impl->fence_value);
        if (FAILED(hr))
            return false;

        if (impl->fence->GetCompletedValue() < impl->fence_value)
        {
            hr = impl->fence->SetEventOnCompletion(
                impl->fence_value,
                impl->fence_event
            );

            if (FAILED(hr))
                return false;

            DWORD res = WaitForSingleObject(impl->fence_event, INFINITE);
            if (res != WAIT_OBJECT_0)
                return false;
        }

        impl->fence_value++;
        return true;
    }

    D3D12_RESOURCE_DESC make_buffer_desc(UINT64 size)
    {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Alignment = 0;
        desc.Width = size;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        return desc;
    }
}

namespace wz::gpu::dx12::internal
{
    GPUHandle upload_scalar_field_texture_dx12(
        Device& device,
        const wz::engine::assets::ScalarFieldData& field)
    {
        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(device.impl);

        if (!impl || !impl->device || !impl->queue || !impl->allocator || !impl->cmd)
            return INVALID_GPU_HANDLE;

        if (!impl->fence || !impl->fence_event)
            return INVALID_GPU_HANDLE;

        if (!impl->scalar_field_srv_heap)
            return INVALID_GPU_HANDLE;

        if (!field.valid())
            return INVALID_GPU_HANDLE;

        if (field.depth != 1)
            return INVALID_GPU_HANDLE;

        if (field.format != wz::engine::assets::ScalarFieldFormat::Float32)
            return INVALID_GPU_HANDLE;

        const uint64_t expected_count =
            static_cast<uint64_t>(field.width) *
            static_cast<uint64_t>(field.height);

        if (expected_count == 0)
            return INVALID_GPU_HANDLE;

        if (field.values.size() != expected_count)
            return INVALID_GPU_HANDLE;

        if (impl->scalar_field_srv_count >= impl->scalar_field_srv_capacity)
            return INVALID_GPU_HANDLE;

        ID3D12Device* d3d = impl->device;

        D3D12_RESOURCE_DESC tex_desc{};
        tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        tex_desc.Alignment = 0;
        tex_desc.Width = field.width;
        tex_desc.Height = field.height;
        tex_desc.DepthOrArraySize = 1;
        tex_desc.MipLevels = 1;
        tex_desc.Format = DXGI_FORMAT_R32_FLOAT;
        tex_desc.SampleDesc.Count = 1;
        tex_desc.SampleDesc.Quality = 0;
        tex_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        tex_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        D3D12_HEAP_PROPERTIES default_heap{};
        default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        default_heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        default_heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        default_heap.CreationNodeMask = 1;
        default_heap.VisibleNodeMask = 1;

        ID3D12Resource* texture = nullptr;

        HRESULT hr = d3d->CreateCommittedResource(
            &default_heap,
            D3D12_HEAP_FLAG_NONE,
            &tex_desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&texture)
        );

        if (FAILED(hr))
            return INVALID_GPU_HANDLE;

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT num_rows = 0;
        UINT64 row_size = 0;
        UINT64 total_bytes = 0;

        d3d->GetCopyableFootprints(
            &tex_desc,
            0,
            1,
            0,
            &footprint,
            &num_rows,
            &row_size,
            &total_bytes
        );

        D3D12_HEAP_PROPERTIES upload_heap{};
        upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        upload_heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        upload_heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        upload_heap.CreationNodeMask = 1;
        upload_heap.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC upload_desc = make_buffer_desc(total_bytes);

        ID3D12Resource* upload = nullptr;

        hr = d3d->CreateCommittedResource(
            &upload_heap,
            D3D12_HEAP_FLAG_NONE,
            &upload_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&upload)
        );

        if (FAILED(hr))
        {
            texture->Release();
            return INVALID_GPU_HANDLE;
        }

        uint8_t* mapped = nullptr;
        hr = upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped));

        if (FAILED(hr))
        {
            upload->Release();
            texture->Release();
            return INVALID_GPU_HANDLE;
        }

        const uint8_t* src =
            reinterpret_cast<const uint8_t*>(field.values.data());

        const uint64_t src_row_bytes =
            static_cast<uint64_t>(field.width) * sizeof(float);

        for (uint32_t y = 0; y < field.height; ++y)
        {
            std::memcpy(
                mapped + footprint.Offset + y * footprint.Footprint.RowPitch,
                src + static_cast<uint64_t>(y) * src_row_bytes,
                static_cast<size_t>(src_row_bytes)
            );
        }

        upload->Unmap(0, nullptr);

        hr = impl->allocator->Reset();
        if (FAILED(hr))
        {
            upload->Release();
            texture->Release();
            return INVALID_GPU_HANDLE;
        }

        hr = impl->cmd->Reset(impl->allocator, nullptr);
        if (FAILED(hr))
        {
            upload->Release();
            texture->Release();
            return INVALID_GPU_HANDLE;
        }

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = texture;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION src_loc{};
        src_loc.pResource = upload;
        src_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src_loc.PlacedFootprint = footprint;

        impl->cmd->CopyTextureRegion(
            &dst,
            0,
            0,
            0,
            &src_loc,
            nullptr
        );

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = texture;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        impl->cmd->ResourceBarrier(1, &barrier);

        hr = impl->cmd->Close();
        if (FAILED(hr))
        {
            upload->Release();
            texture->Release();
            return INVALID_GPU_HANDLE;
        }

        ID3D12CommandList* lists[] = { impl->cmd };
        impl->queue->ExecuteCommandLists(1, lists);

        if (!wait_for_gpu_upload(impl))
        {
            upload->Release();
            texture->Release();
            return INVALID_GPU_HANDLE;
        }

        upload->Release();
        upload = nullptr;

        const uint32_t descriptor_index = impl->scalar_field_srv_count++;

        D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu =
            impl->scalar_field_srv_heap->GetCPUDescriptorHandleForHeapStart();

        srv_cpu.ptr +=
            static_cast<SIZE_T>(descriptor_index) *
            static_cast<SIZE_T>(impl->scalar_field_srv_stride);

        D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu =
            impl->scalar_field_srv_heap->GetGPUDescriptorHandleForHeapStart();

        srv_gpu.ptr +=
            static_cast<UINT64>(descriptor_index) *
            static_cast<UINT64>(impl->scalar_field_srv_stride);

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R32_FLOAT;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MostDetailedMip = 0;
        srv.Texture2D.MipLevels = 1;
        srv.Texture2D.PlaneSlice = 0;
        srv.Texture2D.ResourceMinLODClamp = 0.0f;

        d3d->CreateShaderResourceView(texture, &srv, srv_cpu);

        DX12ScalarFieldTexture stored{};
        stored.texture = texture;
        stored.srv_cpu = srv_cpu;
        stored.srv_gpu = srv_gpu;
        stored.width = field.width;
        stored.height = field.height;

        return impl->scalar_field_textures.add(stored);
    }
}