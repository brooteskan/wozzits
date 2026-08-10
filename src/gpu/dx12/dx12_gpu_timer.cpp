// src/gpu/dx12/dx12_gpu_timer.cpp

#include <gpu/dx12/dx12_gpu_timer.h>
#include "dx12_device_internal.h"

#include <cassert>
#include <cstring>

namespace wz::gpu::dx12
{
    struct GpuTimerImpl
    {
        ID3D12QueryHeap* query_heap  = nullptr;
        ID3D12Resource*  readback    = nullptr;
        uint64_t*        mapped_data = nullptr;

        uint32_t slot_count    = 0;
        uint64_t gpu_frequency = 0;
    };


    namespace
    {
        // Release whatever got built and hand back an INVALID timer (null impl),
        // which is exactly what GpuTimer::valid() is for. Every failure path below
        // goes through here so a partially-built timer never escapes.
        GpuTimer fail_gpu_timer(GpuTimerImpl* impl)
        {
            if (impl)
            {
                // mapped_data is only set after a successful Map, so Unmap is
                // called only when there is a Map to match.
                if (impl->mapped_data && impl->readback)
                {
                    const D3D12_RANGE written{ 0, 0 };
                    impl->readback->Unmap(0, &written);
                }
                if (impl->readback)
                    impl->readback->Release();
                if (impl->query_heap)
                    impl->query_heap->Release();
                delete impl;
            }
            return GpuTimer{};
        }
    }

    GpuTimer create_gpu_timer(Device& device, uint32_t slot_count)
    {
        // Checked, not asserted. Every HRESULT here used to be consumed ONLY by
        // assert(SUCCEEDED(hr)), which NDEBUG strips -- so in Release each failure
        // fell straight through and create_gpu_timer returned a timer that
        // valid() called good with a null query_heap (EndQuery on null in
        // gpu_timer_begin) or, worse, a null mapped_data that the memset at the
        // bottom dereferenced immediately. dx12_check_hr logs the failure and
        // marks the device lost when the HRESULT says so, matching every other
        // call site in this backend.
        assert(device.impl);
        auto* dx = static_cast<DX12Device*>(device.impl);
        if (!dx || !dx->device || !dx->queue)
            return GpuTimer{};

        auto* impl = new GpuTimerImpl{};
        impl->slot_count = slot_count;

        // Query the GPU timestamp frequency from the command queue. A queue that
        // does not support timestamps fails here rather than producing a timer
        // that can only ever report zero.
        HRESULT hr = dx->queue->GetTimestampFrequency(&impl->gpu_frequency);
        if (!dx12_check_hr(*dx, hr, "ID3D12CommandQueue::GetTimestampFrequency")
            || impl->gpu_frequency == 0)
        {
            return fail_gpu_timer(impl);
        }

        // Each slot uses 2 timestamps (begin + end).
        const uint32_t query_count = slot_count * 2;

        // Create timestamp query heap.
        D3D12_QUERY_HEAP_DESC heap_desc{};
        heap_desc.Type     = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        heap_desc.Count    = query_count;
        heap_desc.NodeMask = 0;

        hr = dx->device->CreateQueryHeap(&heap_desc, IID_PPV_ARGS(&impl->query_heap));
        if (!dx12_check_hr(*dx, hr, "ID3D12Device::CreateQueryHeap(timestamp)"))
            return fail_gpu_timer(impl);

        // Create readback buffer for resolved timestamps.
        const uint64_t readback_size = sizeof(uint64_t) * query_count;

        D3D12_HEAP_PROPERTIES heap_props{};
        heap_props.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC res_desc{};
        res_desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
        res_desc.Width              = readback_size;
        res_desc.Height             = 1;
        res_desc.DepthOrArraySize   = 1;
        res_desc.MipLevels          = 1;
        res_desc.SampleDesc.Count   = 1;
        res_desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        hr = dx->device->CreateCommittedResource(
            &heap_props,
            D3D12_HEAP_FLAG_NONE,
            &res_desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&impl->readback));
        if (!dx12_check_hr(
                *dx, hr, "ID3D12Device::CreateCommittedResource(timestamp readback)"))
        {
            return fail_gpu_timer(impl);
        }

        // Persistently map the readback buffer.
        void* mapped = nullptr;
        const D3D12_RANGE read_range{ 0, readback_size };
        hr = impl->readback->Map(0, &read_range, &mapped);
        if (!dx12_check_hr(*dx, hr, "ID3D12Resource::Map(timestamp readback)")
            || mapped == nullptr)
        {
            return fail_gpu_timer(impl);
        }
        impl->mapped_data = static_cast<uint64_t*>(mapped);

        // Zero out initial data.
        std::memset(impl->mapped_data, 0, static_cast<size_t>(readback_size));

        return GpuTimer{ impl };
    }

    void destroy_gpu_timer(GpuTimer& timer)
    {
        if (!timer.impl)
            return;

        if (timer.impl->readback)
        {
            D3D12_RANGE written{ 0, 0 };
            timer.impl->readback->Unmap(0, &written);
            timer.impl->readback->Release();
        }

        if (timer.impl->query_heap)
            timer.impl->query_heap->Release();

        delete timer.impl;
        timer.impl = nullptr;
    }

    void gpu_timer_begin(GpuTimer& timer, Device& device, uint32_t slot)
    {
        assert(timer.impl && slot < timer.impl->slot_count);
        auto* dx = static_cast<DX12Device*>(device.impl);

        // Begin timestamp = slot * 2
        dx->cmd->EndQuery(timer.impl->query_heap, D3D12_QUERY_TYPE_TIMESTAMP, slot * 2);
    }

    void gpu_timer_end(GpuTimer& timer, Device& device, uint32_t slot)
    {
        assert(timer.impl && slot < timer.impl->slot_count);
        auto* dx = static_cast<DX12Device*>(device.impl);

        // End timestamp = slot * 2 + 1
        dx->cmd->EndQuery(timer.impl->query_heap, D3D12_QUERY_TYPE_TIMESTAMP, slot * 2 + 1);
    }

    void gpu_timer_resolve(GpuTimer& timer, Device& device)
    {
        assert(timer.impl);
        auto* dx = static_cast<DX12Device*>(device.impl);

        const uint32_t query_count = timer.impl->slot_count * 2;

        // ResolveQueryData copies timestamps from the query heap into the
        // readback buffer.  This must happen on the command list BEFORE it
        // is closed + submitted, so call this before end_frame().
        //
        // However, the caller typically wants results AFTER end_frame()'s
        // fence wait.  So we resolve into the readback buffer that is already
        // persistently mapped; the fence guarantees the data is available.
        dx->cmd->ResolveQueryData(
            timer.impl->query_heap,
            D3D12_QUERY_TYPE_TIMESTAMP,
            0,
            query_count,
            timer.impl->readback,
            0);
    }

    double gpu_timer_elapsed_ms(const GpuTimer& timer, uint32_t slot)
    {
        assert(timer.impl && slot < timer.impl->slot_count);

        const uint64_t begin_ts = timer.impl->mapped_data[slot * 2];
        const uint64_t end_ts   = timer.impl->mapped_data[slot * 2 + 1];

        if (end_ts <= begin_ts || timer.impl->gpu_frequency == 0)
            return 0.0;

        const uint64_t delta = end_ts - begin_ts;
        return static_cast<double>(delta) * 1000.0 / static_cast<double>(timer.impl->gpu_frequency);
    }
}
