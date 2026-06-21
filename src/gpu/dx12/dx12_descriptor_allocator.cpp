// src/gpu/dx12/dx12_descriptor_allocator.cpp

#include <gpu/dx12/dx12_descriptor_allocator.h>

#include <algorithm>
#include <cassert>

namespace wz::gpu::dx12
{
    namespace
    {
        DX12DescriptorTable make_table(
            ID3D12DescriptorHeap* heap,
            uint32_t slot,
            uint32_t count,
            uint32_t stride)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE cpu =
                heap->GetCPUDescriptorHandleForHeapStart();
            cpu.ptr += static_cast<SIZE_T>(slot) * stride;

            D3D12_GPU_DESCRIPTOR_HANDLE gpu =
                heap->GetGPUDescriptorHandleForHeapStart();
            gpu.ptr += static_cast<UINT64>(slot) * stride;

            return DX12DescriptorTable{ cpu, gpu, count, stride };
        }
    }

    bool DX12DescriptorAllocator::init(ID3D12Device* device, uint32_t capacity)
    {
        assert(device);
        assert(capacity > 0);

        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = capacity;
        desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        desc.NodeMask       = 0;

        HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap_));
        if (FAILED(hr))
            return false;

        device_   = device;
        stride_   = device->GetDescriptorHandleIncrementSize(
                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        capacity_ = capacity;
        next_     = 0;
        free_ranges_.clear();
        return true;
    }

    void DX12DescriptorAllocator::destroy()
    {
        if (heap_)
        {
            heap_->Release();
            heap_ = nullptr;
        }
        device_   = nullptr;
        stride_   = 0;
        capacity_ = 0;
        next_     = 0;
        free_ranges_.clear();
    }

    void DX12DescriptorAllocator::create_structured_buffer_srv(
        const DX12DescriptorTable& table,
        uint32_t                   offset,
        ID3D12Resource*            resource,
        uint32_t                   element_count,
        uint32_t                   stride_bytes)
    {
        assert(device_);
        assert(resource);
        assert(offset < table.count);

        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
        srv_desc.Format                     = DXGI_FORMAT_UNKNOWN;
        srv_desc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Buffer.FirstElement        = 0;
        srv_desc.Buffer.NumElements         = element_count;
        srv_desc.Buffer.StructureByteStride = stride_bytes;
        srv_desc.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_NONE;

        device_->CreateShaderResourceView(resource, &srv_desc, table.cpu_at(offset));
    }

    void DX12DescriptorAllocator::create_structured_buffer_uav(
        const DX12DescriptorTable& table,
        uint32_t                   offset,
        ID3D12Resource*            resource,
        uint32_t                   element_count,
        uint32_t                   stride_bytes)
    {
        assert(device_);
        assert(resource);
        assert(offset < table.count);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav_desc.Format = DXGI_FORMAT_UNKNOWN;
        uav_desc.Buffer.FirstElement = 0;
        uav_desc.Buffer.NumElements = element_count;
        uav_desc.Buffer.StructureByteStride = stride_bytes;
        uav_desc.Buffer.CounterOffsetInBytes = 0;
        uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

        device_->CreateUnorderedAccessView(
            resource,
            nullptr,
            &uav_desc,
            table.cpu_at(offset));
    }

    DX12DescriptorTable DX12DescriptorAllocator::allocate(uint32_t count)
    {
        assert(count > 0);

        if (!heap_)
            return {};

        for (auto it = free_ranges_.begin(); it != free_ranges_.end(); ++it) {
            if (it->count < count) {
                continue;
            }

            const uint32_t slot = it->start;
            it->start += count;
            it->count -= count;
            if (it->count == 0u) {
                free_ranges_.erase(it);
            }
            return make_table(heap_, slot, count, stride_);
        }

        if ((next_ + count) > capacity_)
            return {};

        const uint32_t slot = next_;
        next_ += count;

        return make_table(heap_, slot, count, stride_);
    }

    void DX12DescriptorAllocator::release(const DX12DescriptorTable& table)
    {
        if (!heap_ || !table.valid() || table.stride != stride_) {
            return;
        }

        const D3D12_CPU_DESCRIPTOR_HANDLE heap_cpu =
            heap_->GetCPUDescriptorHandleForHeapStart();
        if (table.cpu_start.ptr < heap_cpu.ptr) {
            return;
        }

        const SIZE_T byte_offset = table.cpu_start.ptr - heap_cpu.ptr;
        if ((byte_offset % stride_) != 0u) {
            return;
        }

        const uint32_t slot =
            static_cast<uint32_t>(byte_offset / stride_);
        if (slot >= capacity_ || table.count > capacity_ - slot) {
            return;
        }

        FreeRange released{ slot, table.count };
        auto it = free_ranges_.begin();
        while (it != free_ranges_.end() && it->start < released.start) {
            ++it;
        }
        it = free_ranges_.insert(it, released);

        if (it != free_ranges_.begin()) {
            auto prev = it - 1;
            if (prev->start + prev->count >= it->start) {
                const uint32_t end =
                    (std::max)(prev->start + prev->count,
                               it->start + it->count);
                prev->count = end - prev->start;
                it = free_ranges_.erase(it);
                it = prev;
            }
        }

        auto next = it + 1;
        while (next != free_ranges_.end()
            && it->start + it->count >= next->start)
        {
            const uint32_t end =
                (std::max)(it->start + it->count,
                           next->start + next->count);
            it->count = end - it->start;
            next = free_ranges_.erase(next);
        }

        while (!free_ranges_.empty()) {
            const FreeRange& tail = free_ranges_.back();
            if (tail.start + tail.count != next_) {
                break;
            }
            next_ = tail.start;
            free_ranges_.pop_back();
        }
    }
}
