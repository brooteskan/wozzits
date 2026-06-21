#pragma once
// gpu/dx12/dx12_descriptor_allocator.h
//
// Descriptor-range allocator over a single shader-visible CBV/SRV/UAV heap.
// Ranges can be released and reused by long-lived GPU resources that are
// created/destroyed independently.

#include <d3d12.h>
#include <cstdint>
#include <vector>

struct ID3D12Device;

namespace wz::gpu::dx12
{
    // A contiguous range of descriptors in a heap.
    struct DX12DescriptorTable
    {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_start{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_start{};
        uint32_t count  = 0;
        uint32_t stride = 0;  // bytes between adjacent descriptors

        bool valid() const noexcept { return count > 0 && stride > 0; }

        D3D12_CPU_DESCRIPTOR_HANDLE cpu_at(uint32_t i) const noexcept
        {
            return { cpu_start.ptr + static_cast<SIZE_T>(i) * stride };
        }

        D3D12_GPU_DESCRIPTOR_HANDLE gpu_at(uint32_t i) const noexcept
        {
            return { gpu_start.ptr + static_cast<UINT64>(i) * stride };
        }
    };

    // Shader-visible CBV/SRV/UAV heap with linear suballocation.
    class DX12DescriptorAllocator
    {
    public:
        // Create the underlying heap.  Returns false on failure.
        bool init(ID3D12Device* device, uint32_t capacity);

        void destroy();

        // Allocate a contiguous range of `count` descriptors.
        // Returns an invalid table if capacity is exhausted.
        DX12DescriptorTable allocate(uint32_t count = 1);

        // Release a range previously returned by allocate(). Invalid tables,
        // tables from another heap, and out-of-range tables are ignored.
        void release(const DX12DescriptorTable& table);

        // Write a StructuredBuffer SRV for `resource` into slot `offset` of `table`.
        // Requires init() to have succeeded.  `element_count` is the number of
        // structs in the buffer; `stride_bytes` is sizeof one struct.
        void create_structured_buffer_srv(
            const DX12DescriptorTable& table,
            uint32_t                   offset,
            ID3D12Resource*            resource,
            uint32_t                   element_count,
            uint32_t                   stride_bytes);

        // Write a StructuredBuffer UAV for `resource` into slot `offset` of
        // `table`. No counter resource is used.
        void create_structured_buffer_uav(
            const DX12DescriptorTable& table,
            uint32_t                   offset,
            ID3D12Resource*            resource,
            uint32_t                   element_count,
            uint32_t                   stride_bytes);

        // Reset the allocator — all previously returned tables become invalid.
        void reset() noexcept
        {
            next_ = 0;
            free_ranges_.clear();
        }

        ID3D12Device*         device()   const noexcept { return device_; }
        ID3D12DescriptorHeap* heap()     const noexcept { return heap_; }
        uint32_t              stride()   const noexcept { return stride_; }
        uint32_t              capacity() const noexcept { return capacity_; }
        uint32_t              used()     const noexcept { return next_; }

    private:
        struct FreeRange
        {
            uint32_t start = 0;
            uint32_t count = 0;
        };

        ID3D12Device*         device_   = nullptr;
        ID3D12DescriptorHeap* heap_     = nullptr;
        uint32_t              stride_   = 0;
        uint32_t              capacity_ = 0;
        uint32_t              next_     = 0;
        std::vector<FreeRange> free_ranges_{};
    };
}
