// file: src/gpu/dx12/dx12_device.cpp

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_4.h>

#include <wrl/client.h>

#include <gpu/gpu.h>
#include <window/window2.h>
#include <cassert>

namespace wz::gpu::dx12
{
    struct DX12Device
    {
        //fences
        ID3D12Fence* fence = nullptr;
        HANDLE fence_event = nullptr;
        UINT64 fence_value = 0;

        // core
        ID3D12Device* device = nullptr;
        IDXGISwapChain3* swapchain = nullptr;
        ID3D12CommandQueue* queue = nullptr;

        // frame
        ID3D12CommandAllocator* allocator = nullptr;
        ID3D12GraphicsCommandList* cmd = nullptr;

        // render target
        ID3D12DescriptorHeap* rtv_heap = nullptr;
        ID3D12Resource* backbuffers[2] = {};
        UINT rtv_stride = 0;

        UINT frame_index = 0;

        HWND hwnd = nullptr;
        UINT width = 0;
        UINT height = 0;
    };

    Device create_device(void* native_window)
    {
        HWND hwnd = static_cast<HWND>(native_window);

        HRESULT hr;
        IDXGIFactory4* factory = nullptr;
        hr = CreateDXGIFactory2(
            0,
            IID_PPV_ARGS(&factory)
        );
        assert(SUCCEEDED(hr));

        ID3D12Device* device = nullptr;
        hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
        assert(SUCCEEDED(hr));

        D3D12_COMMAND_QUEUE_DESC qdesc = {};
        qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

        ID3D12CommandQueue* queue = nullptr;
        hr = device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&queue));
        assert(SUCCEEDED(hr));

        // ────── create swapchain ───────────────────────────────────────────────────────
        DXGI_SWAP_CHAIN_DESC1 scdesc = {};
        scdesc.BufferCount = 2;
        scdesc.Width = 1280;
        scdesc.Height = 720;
        scdesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // attention: hardcoded format, must match in resize()
        scdesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scdesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scdesc.SampleDesc.Count = 1;

        IDXGISwapChain1* temp = nullptr;

        assert(hwnd != nullptr);
        assert(IsWindow(hwnd));

        hr = factory->CreateSwapChainForHwnd(
            queue,
            hwnd,
            &scdesc,
            nullptr,
            nullptr,
            &temp
        );
        assert(SUCCEEDED(hr));

        hr = factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        assert(SUCCEEDED(hr));

        IDXGISwapChain3* swapchain = nullptr;
        hr = temp->QueryInterface(IID_PPV_ARGS(&swapchain));
        assert(SUCCEEDED(hr));

        

        factory->Release();
        temp->Release();

        // ────── create rtv heap ───────────────────────────────────────────────────────
        D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
        heap_desc.NumDescriptors = 2;
        heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

        ID3D12DescriptorHeap* rtv_heap = nullptr;
        hr = device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&rtv_heap));
        assert(SUCCEEDED(hr));

        // ────── create render target views ───────────────────────────────────────────────────────
        UINT rtv_stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        auto handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();

        ID3D12Resource* backbuffers[2] = {};
        for (UINT i = 0; i < 2; ++i)
        {
            swapchain->GetBuffer(i, IID_PPV_ARGS(&backbuffers[i]));
            device->CreateRenderTargetView(backbuffers[i], nullptr, handle);

            handle.ptr += rtv_stride;
        }

        // ────── command allocator + list ───────────────────────────────────────────────────────
        ID3D12CommandAllocator* allocator = nullptr;
        hr = device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&allocator)
        );
        assert(SUCCEEDED(hr));

        ID3D12GraphicsCommandList* cmd = nullptr;
        hr = device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocator,
            nullptr,
            IID_PPV_ARGS(&cmd)
        );
        assert(SUCCEEDED(hr));

        hr = cmd->Close();
        assert(SUCCEEDED(hr));

        // ────── store everything ───────────────────────────────────────────────────────
        DX12Device* impl = new DX12Device{};
        impl->device = device;
        impl->swapchain = swapchain;
        impl->queue = queue;
        impl->allocator = allocator;
        impl->cmd = cmd;
        impl->rtv_heap = rtv_heap;
        impl->hwnd = hwnd;
        impl->width = 1280;
        impl->height = 720;

        // ────── initialize fences ───────────────────────────────────────────────────────
        hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&impl->fence));
        assert(SUCCEEDED(hr));

        impl->fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        impl->fence_value = 1;

        // ────── store backbuffers ───────────────────────────────────────────────────────
        for (UINT i = 0; i < 2; ++i)
        {
            impl->backbuffers[i] = backbuffers[i];
        }

        impl->rtv_stride = rtv_stride;

        // ────── return ───────────────────────────────────────────────────────
        Device out{};
        out.impl = impl;
        return out;
    }

    void begin_frame(Device& d)
    {
        HRESULT hr;
        auto* impl = (DX12Device*)d.impl;

        impl->frame_index = impl->swapchain->GetCurrentBackBufferIndex();

        hr = impl->allocator->Reset();
        assert(SUCCEEDED(hr));

        hr = impl->cmd->Reset(impl->allocator, nullptr);
        assert(SUCCEEDED(hr));

        // transition to render target
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = impl->backbuffers[impl->frame_index];
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

        impl->cmd->ResourceBarrier(1, &barrier);

        // ────── RTV BINDING ─────────────────────────────────────────────
        auto rtv_handle =
            impl->rtv_heap->GetCPUDescriptorHandleForHeapStart();

        rtv_handle.ptr += impl->frame_index * impl->rtv_stride;

        impl->cmd->OMSetRenderTargets(
            1,
            &rtv_handle,
            FALSE,
            nullptr
        );


        // ────── viewport + scissor ───────────────────────────────────────────────────────
        D3D12_VIEWPORT vp = {};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = static_cast<float>(impl->width);
        vp.Height = static_cast<float>(impl->height);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;

        D3D12_RECT scissor = {};
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = static_cast<LONG>(impl->width);
        scissor.bottom = static_cast<LONG>(impl->height);

        impl->cmd->RSSetViewports(1, &vp);
        impl->cmd->RSSetScissorRects(1, &scissor);
    }

    void clear(Device& d, float r, float g, float b, float a)
    {
        auto* impl = (DX12Device*)d.impl;

        auto handle = impl->rtv_heap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += impl->frame_index * impl->rtv_stride;

        float color[4] = { r, g, b, a };

        impl->cmd->ClearRenderTargetView(handle, color, 0, nullptr);
    }

    void end_frame(Device& d)
    {
        HRESULT hr;
        auto* impl = (DX12Device*)d.impl;

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = impl->backbuffers[impl->frame_index];
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

        impl->cmd->ResourceBarrier(1, &barrier);

        hr = impl->cmd->Close();
        assert(SUCCEEDED(hr));

        ID3D12CommandList* lists[] = { impl->cmd };
        impl->queue->ExecuteCommandLists(1, lists);

        // ────── wait for fences ───────────────────────────────────────────────────────
        hr = impl->queue->Signal(impl->fence, impl->fence_value);
        assert(SUCCEEDED(hr));

        if (impl->fence->GetCompletedValue() < impl->fence_value)
        {
            hr = impl->fence->SetEventOnCompletion(impl->fence_value, impl->fence_event);
            assert(SUCCEEDED(hr));

            DWORD res = WaitForSingleObject(impl->fence_event, INFINITE);
            assert(res == WAIT_OBJECT_0);
        }

        impl->fence_value++;
    }

    void present(Device& d)
    {
        HRESULT hr;
        auto* impl = (DX12Device*)d.impl;
        hr = impl->swapchain->Present(1, 0);
        assert(SUCCEEDED(hr));

    }

    void destroy_device(Device& d)
    {
        auto* impl = (DX12Device*)d.impl;

        if (!impl) return;

        for (int i = 0; i < 2; ++i)
            if (impl->backbuffers[i]) impl->backbuffers[i]->Release();

        if (impl->rtv_heap) impl->rtv_heap->Release();
        if (impl->cmd) impl->cmd->Release();
        if (impl->allocator) impl->allocator->Release();
        if (impl->queue) impl->queue->Release();
        if (impl->swapchain) impl->swapchain->Release();
        if (impl->device) impl->device->Release();

        if (impl->fence) impl->fence->Release();
        if (impl->fence_event) CloseHandle(impl->fence_event);

        delete impl;
        d.impl = nullptr;
    }


    // ────── resize ───────────────────────────────────────────────────────
    namespace
    {

            void wait_for_gpu(DX12Device* impl)
            {
                HRESULT hr;

                // Signal GPU with current fence value
                hr = impl->queue->Signal(impl->fence, impl->fence_value);
                assert(SUCCEEDED(hr));

                // If GPU hasn't reached this fence value yet → wait
                if (impl->fence->GetCompletedValue() < impl->fence_value)
                {
                    hr = impl->fence->SetEventOnCompletion(
                        impl->fence_value,
                        impl->fence_event
                    );
                    assert(SUCCEEDED(hr));

                    DWORD res = WaitForSingleObject(
                        impl->fence_event,
                        INFINITE
                    );

                    assert(res == WAIT_OBJECT_0);
                }

                // advance fence for next use
                impl->fence_value++;
            }
        
    }

    void resize(Device& d, int w, int h)
    {
        auto* impl = (DX12Device*)d.impl;
        if (!impl || !impl->swapchain)
            return;

        impl->width = w;
        impl->height = h;

        // 1. ensure GPU is idle
        wait_for_gpu(impl);

        // 2. release current backbuffers
        for (int i = 0; i < 2; ++i)
        {
            if (impl->backbuffers[i])
            {
                impl->backbuffers[i]->Release();
                impl->backbuffers[i] = nullptr;
            }
        }

        // 3. resize swapchain buffers
        HRESULT hr = impl->swapchain->ResizeBuffers(
            2,
            w,
            h,
            DXGI_FORMAT_R8G8B8A8_UNORM,// attention: hardcoded format
            0
        );
        assert(SUCCEEDED(hr));

        // 4. reacquire backbuffers
        auto handle = impl->rtv_heap->GetCPUDescriptorHandleForHeapStart();

        for (UINT i = 0; i < 2; ++i)
        {
            hr = impl->swapchain->GetBuffer(i, IID_PPV_ARGS(&impl->backbuffers[i]));
            assert(SUCCEEDED(hr));

            impl->device->CreateRenderTargetView(
                impl->backbuffers[i],
                nullptr,
                handle
            );

            handle.ptr += impl->rtv_stride;
        }

        impl->rtv_stride =
            impl->device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_RTV
            );
    }
} // namespace wz::gpu::dx12