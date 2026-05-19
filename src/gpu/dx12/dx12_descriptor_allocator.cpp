// src/gpu/dx12/dx12_descriptor_allocator.cpp

#include <gpu/dx12/dx12_descriptor_allocator.h>

#include <cassert>

namespace wz::gpu::dx12
{
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

        stride_   = device->GetDescriptorHandleIncrementSize(
                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        capacity_ = capacity;
        next_     = 0;
        return true;
    }

    void DX12DescriptorAllocator::destroy()
    {
        if (heap_)
        {
            heap_->Release();
            heap_ = nullptr;
        }
        stride_   = 0;
        capacity_ = 0;
        next_     = 0;
    }

    DX12DescriptorTable DX12DescriptorAllocator::allocate(uint32_t count)
    {
        assert(count > 0);

        if (!heap_ || (next_ + count) > capacity_)
            return {};

        const uint32_t slot = next_;
        next_ += count;

        D3D12_CPU_DESCRIPTOR_HANDLE cpu =
            heap_->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += static_cast<SIZE_T>(slot) * stride_;

        D3D12_GPU_DESCRIPTOR_HANDLE gpu =
            heap_->GetGPUDescriptorHandleForHeapStart();
        gpu.ptr += static_cast<UINT64>(slot) * stride_;

        return DX12DescriptorTable{ cpu, gpu, count, stride_ };
    }
}
