// src/gpu/dx12/dx12_texture.cpp
//
// Generic (asset-agnostic) committed-texture creation + upload. Generalizes
// dx12_scalar_field_texture.cpp: same R32_FLOAT Texture2D upload dance, but no
// dedicated SRV heap and parameterized by format/dimension. Backs the rhi
// GpuResourceRegistry's texture resources via engine/rendering/rhi_gpu_backend.

#include "dx12_device_internal.h"

#include <gpu/dx12/dx12_internal.h>
#include <gpu/gpu_resource_types.h>
#include <gpu/texture.h>

#include <d3d12.h>

#include <cstdint>
#include <cstring>

namespace wz::gpu::dx12::internal
{
    namespace
    {
        DXGI_FORMAT to_dxgi_format(wz::gpu::TextureFormat format) noexcept
        {
            switch (format) {
            case wz::gpu::TextureFormat::R32Float:
                return DXGI_FORMAT_R32_FLOAT;
            }
            return DXGI_FORMAT_UNKNOWN;
        }

        uint32_t texel_bytes(DXGI_FORMAT format) noexcept
        {
            switch (format) {
            case DXGI_FORMAT_R32_FLOAT:
                return 4u;
            default:
                return 0u;
            }
        }

        D3D12_RESOURCE_DIMENSION to_d3d12_dimension(
            wz::gpu::TextureDimension dimension) noexcept
        {
            return dimension == wz::gpu::TextureDimension::Texture3D
                ? D3D12_RESOURCE_DIMENSION_TEXTURE3D
                : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        }

        // The resting state for a sampled texture: readable from any shader
        // stage, since the SRG/draw path may sample it in either VS or PS.
        constexpr D3D12_RESOURCE_STATES kShaderReadState =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
            | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

        D3D12_RESOURCE_DESC make_texture_desc(
            const DX12Texture& tex,
            wz::gpu::TextureDimension dimension)
        {
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = to_d3d12_dimension(dimension);
            desc.Alignment = 0;
            desc.Width = tex.width;
            desc.Height = tex.height;
            desc.DepthOrArraySize = static_cast<UINT16>(tex.depth);
            desc.MipLevels = 1;
            desc.Format = tex.format;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            desc.Flags = D3D12_RESOURCE_FLAG_NONE;
            return desc;
        }

        D3D12_RESOURCE_DESC make_upload_buffer_desc(UINT64 size)
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

        bool wait_for_gpu(DX12Device* impl)
        {
            HRESULT hr = impl->queue->Signal(impl->fence, impl->fence_value);
            if (FAILED(hr)) {
                return false;
            }
            if (impl->fence->GetCompletedValue() < impl->fence_value) {
                hr = impl->fence->SetEventOnCompletion(
                    impl->fence_value, impl->fence_event);
                if (FAILED(hr)) {
                    return false;
                }
                if (WaitForSingleObject(impl->fence_event, INFINITE)
                    != WAIT_OBJECT_0)
                {
                    return false;
                }
            }
            ++impl->fence_value;
            return true;
        }
    }

    // ── DX12TextureTable ─────────────────────────────────────────────

    DX12TextureTable::DX12TextureTable()
    {
        slots_.emplace_back();
    }

    GPUHandle DX12TextureTable::add(DX12Texture tex)
    {
        if (!tex.valid()) {
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
            slot.tex = tex;
            return GPUHandle{
                .id = id,
                .epoch = slot.epoch,
                .type = kGPUTextureResourceType,
            };
        }

        Slot slot{};
        slot.epoch = 1u;
        slot.occupied = true;
        slot.tex = tex;
        slots_.push_back(slot);
        return GPUHandle{
            .id = static_cast<uint32_t>(slots_.size() - 1u),
            .epoch = slot.epoch,
            .type = kGPUTextureResourceType,
        };
    }

    DX12Texture* DX12TextureTable::get(GPUHandle handle)
    {
        return const_cast<DX12Texture*>(
            static_cast<const DX12TextureTable*>(this)->get(handle));
    }

    const DX12Texture* DX12TextureTable::get(GPUHandle handle) const
    {
        if (!handle.valid()
            || handle.type != kGPUTextureResourceType
            || handle.id == 0u
            || handle.id >= slots_.size())
        {
            return nullptr;
        }
        const Slot& slot = slots_[handle.id];
        if (!slot.occupied || slot.epoch != handle.epoch) {
            return nullptr;
        }
        return &slot.tex;
    }

    bool DX12TextureTable::release(GPUHandle handle)
    {
        if (!handle.valid()
            || handle.type != kGPUTextureResourceType
            || handle.id == 0u
            || handle.id >= slots_.size())
        {
            return false;
        }
        Slot& slot = slots_[handle.id];
        if (!slot.occupied || slot.epoch != handle.epoch) {
            return false;
        }
        if (slot.tex.texture) {
            slot.tex.texture->Release();
        }
        slot.tex = DX12Texture{};
        slot.occupied = false;
        ++slot.epoch;
        if (slot.epoch == 0u) {
            slot.epoch = 1u;
        }
        return true;
    }

    void DX12TextureTable::destroy()
    {
        for (Slot& slot : slots_) {
            if (slot.occupied && slot.tex.texture) {
                slot.tex.texture->Release();
            }
            slot.tex = DX12Texture{};
            slot.occupied = false;
            ++slot.epoch;
        }
        slots_.clear();
        slots_.emplace_back();
    }

    // ── create / update / release ────────────────────────────────────

    GPUHandle create_texture_dx12(
        Device& device,
        const wz::gpu::TextureDesc& desc)
    {
        auto* impl = static_cast<DX12Device*>(device.impl);
        if (!impl || !impl->device || !desc.valid()) {
            return INVALID_GPU_HANDLE;
        }

        const DXGI_FORMAT format = to_dxgi_format(desc.format);
        if (format == DXGI_FORMAT_UNKNOWN) {
            return INVALID_GPU_HANDLE;
        }

        DX12Texture tex{};
        tex.width = desc.width;
        tex.height = desc.height;
        tex.depth = desc.depth;
        tex.format = format;
        // Created ready to receive its first upload.
        tex.state = D3D12_RESOURCE_STATE_COPY_DEST;

        const D3D12_RESOURCE_DESC resource_desc =
            make_texture_desc(tex, desc.dimension);

        D3D12_HEAP_PROPERTIES default_heap{};
        default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        default_heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        default_heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        default_heap.CreationNodeMask = 1;
        default_heap.VisibleNodeMask = 1;

        HRESULT hr = impl->device->CreateCommittedResource(
            &default_heap,
            D3D12_HEAP_FLAG_NONE,
            &resource_desc,
            tex.state,
            nullptr,
            IID_PPV_ARGS(&tex.texture));
        if (FAILED(hr) || !tex.texture) {
            return INVALID_GPU_HANDLE;
        }

        return impl->textures.add(tex);
    }

    bool update_texture_dx12(
        Device& device,
        GPUHandle handle,
        const void* data,
        uint64_t byte_count,
        uint64_t byte_offset)
    {
        auto* impl = static_cast<DX12Device*>(device.impl);
        if (!impl || !impl->device || !impl->queue || !impl->fence
            || !impl->fence_event)
        {
            return false;
        }
        if (!data || byte_count == 0u || byte_offset != 0u) {
            return false;
        }

        DX12Texture* tex = impl->textures.get(handle);
        if (!tex || !tex->valid()) {
            return false;
        }

        const uint32_t bpp = texel_bytes(tex->format);
        if (bpp == 0u) {
            return false;
        }
        const uint64_t expected =
            static_cast<uint64_t>(tex->width)
            * static_cast<uint64_t>(tex->height)
            * static_cast<uint64_t>(tex->depth)
            * static_cast<uint64_t>(bpp);
        if (byte_count != expected) {
            return false;
        }

        ID3D12Device* d3d = impl->device;
        const D3D12_RESOURCE_DESC tex_desc = tex->texture->GetDesc();

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT num_rows = 0;
        UINT64 row_size = 0;
        UINT64 total_bytes = 0;
        d3d->GetCopyableFootprints(
            &tex_desc, 0, 1, 0, &footprint, &num_rows, &row_size, &total_bytes);

        D3D12_HEAP_PROPERTIES upload_heap{};
        upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        upload_heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        upload_heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        upload_heap.CreationNodeMask = 1;
        upload_heap.VisibleNodeMask = 1;

        const D3D12_RESOURCE_DESC upload_desc =
            make_upload_buffer_desc(total_bytes);

        ID3D12Resource* upload = nullptr;
        HRESULT hr = d3d->CreateCommittedResource(
            &upload_heap,
            D3D12_HEAP_FLAG_NONE,
            &upload_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&upload));
        if (FAILED(hr)) {
            return false;
        }

        uint8_t* mapped = nullptr;
        hr = upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
        if (FAILED(hr)) {
            upload->Release();
            return false;
        }

        // Row-major copy honoring the upload row pitch, iterating depth slices so
        // both Texture2D (depth == 1) and Texture3D upload correctly.
        const auto* src = static_cast<const uint8_t*>(data);
        const uint64_t src_row_bytes =
            static_cast<uint64_t>(tex->width) * static_cast<uint64_t>(bpp);
        const uint64_t dst_slice_pitch =
            static_cast<uint64_t>(footprint.Footprint.RowPitch)
            * static_cast<uint64_t>(num_rows);
        for (uint32_t z = 0; z < tex->depth; ++z) {
            for (uint32_t y = 0; y < num_rows; ++y) {
                std::memcpy(
                    mapped + footprint.Offset
                        + z * dst_slice_pitch
                        + static_cast<uint64_t>(y) * footprint.Footprint.RowPitch,
                    src + (static_cast<uint64_t>(z) * num_rows + y) * src_row_bytes,
                    static_cast<size_t>(src_row_bytes));
            }
        }
        upload->Unmap(0, nullptr);

        ID3D12CommandAllocator* allocator = nullptr;
        hr = d3d->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
        if (FAILED(hr)) {
            upload->Release();
            return false;
        }

        ID3D12GraphicsCommandList* cmd = nullptr;
        hr = d3d->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
            IID_PPV_ARGS(&cmd));
        if (FAILED(hr)) {
            allocator->Release();
            upload->Release();
            return false;
        }

        if (tex->state != D3D12_RESOURCE_STATE_COPY_DEST) {
            D3D12_RESOURCE_BARRIER to_copy{};
            to_copy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            to_copy.Transition.pResource = tex->texture;
            to_copy.Transition.StateBefore = tex->state;
            to_copy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            to_copy.Transition.Subresource =
                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmd->ResourceBarrier(1, &to_copy);
        }

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = tex->texture;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION src_loc{};
        src_loc.pResource = upload;
        src_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src_loc.PlacedFootprint = footprint;

        cmd->CopyTextureRegion(&dst, 0, 0, 0, &src_loc, nullptr);

        D3D12_RESOURCE_BARRIER to_read{};
        to_read.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        to_read.Transition.pResource = tex->texture;
        to_read.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        to_read.Transition.StateAfter = kShaderReadState;
        to_read.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &to_read);

        hr = cmd->Close();
        if (FAILED(hr)) {
            cmd->Release();
            allocator->Release();
            upload->Release();
            return false;
        }

        ID3D12CommandList* lists[] = { cmd };
        impl->queue->ExecuteCommandLists(1, lists);
        const bool waited = wait_for_gpu(impl);

        cmd->Release();
        allocator->Release();
        upload->Release();

        if (!waited) {
            return false;
        }
        tex->state = kShaderReadState;
        return true;
    }

    bool release_texture_dx12(Device& device, GPUHandle handle)
    {
        auto* impl = static_cast<DX12Device*>(device.impl);
        if (!impl) {
            return false;
        }
        return impl->textures.release(handle);
    }
}
