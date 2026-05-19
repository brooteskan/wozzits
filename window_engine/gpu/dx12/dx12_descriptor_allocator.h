#pragma once
// gpu/dx12/dx12_descriptor_allocator.h
//
// Linear bump allocator over a single shader-visible CBV/SRV/UAV heap.
// Descriptors are never freed individually; the whole allocator is reset
// or destroyed at once.  Suitable for per-frame or persistent static
// descriptor tables.

#include <d3d12.h>
#include <cstdint>

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

        // Reset the allocator — all previously returned tables become invalid.
        void reset() noexcept { next_ = 0; }

        ID3D12DescriptorHeap* heap()     const noexcept { return heap_; }
        uint32_t              stride()   const noexcept { return stride_; }
        uint32_t              capacity() const noexcept { return capacity_; }
        uint32_t              used()     const noexcept { return next_; }

    private:
        ID3D12DescriptorHeap* heap_     = nullptr;
        uint32_t              stride_   = 0;
        uint32_t              capacity_ = 0;
        uint32_t              next_     = 0;
    };
}
