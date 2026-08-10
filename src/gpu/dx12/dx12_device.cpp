// file: src/gpu/dx12/dx12_device.cpp


#include "dx12_device_internal.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wswitch"
#include <gpu/dx12/external/d3dx12.h>
#pragma clang diagnostic pop
#include <wrl/client.h>

#include <gpu/gpu.h>
#include <gpu/dx12/dx12.h>
#include <diagnostics/diagnostic_sink.h>  // publish debug-layer diagnostics (#291)
#include <window/window2.h>

#include <cstdlib>
#include <cassert>
#include <cstdio>

// #include <engine/render/test/test_triangle_scene.h>
#include <iostream>
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")


namespace
{
    static constexpr DXGI_FORMAT BACKBUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
    static constexpr DXGI_FORMAT DEPTH_FORMAT      = DXGI_FORMAT_D32_FLOAT;

    namespace
    {
        struct alignas(16) TransformConstants
        {
            float world[16];
            float view_proj[16];
        };
    }


    // Install the debug layer's SEVERITY storage filter: store only CORRUPTION/
    // ERROR/WARNING, so INFO/MESSAGE -- per-resource-creation chatter -- never reach
    // storage. This is the volume cap #291 keeps.
    //
    // The old per-id deny list (deny an id after it repeated past 8) is GONE:
    // state-side dedup on the LoggerService lane keeps the exact repeat count now,
    // so the source no longer has to stop storing an id to keep the log quiet
    // (which is what lost the count). Just the severity allow-list, installed once
    // at device creation. Pop-before-push stays so a second call can't grow the
    // stack; a failed push leaves the prior filter in place (the drain runs each
    // frame regardless now, so a momentarily absent cap is harmless).
    void install_debug_storage_filter(wz::gpu::dx12::DX12Device* impl)
    {
        if (!impl || !impl->info_queue) {
            return;
        }

        D3D12_MESSAGE_SEVERITY kept[] = {
            D3D12_MESSAGE_SEVERITY_CORRUPTION,
            D3D12_MESSAGE_SEVERITY_ERROR,
            D3D12_MESSAGE_SEVERITY_WARNING,
        };
        D3D12_INFO_QUEUE_FILTER filter{};
        filter.AllowList.NumSeverities = _countof(kept);
        filter.AllowList.pSeverityList = kept;

        if (impl->debug_filter_pushed) {
            impl->info_queue->PopStorageFilter();
            impl->debug_filter_pushed = false;
        }
        if (SUCCEEDED(impl->info_queue->PushStorageFilter(&filter))) {
            impl->debug_filter_pushed = true;
        }
    }

    // Create the device-shared depth buffer + DSV.  Caller must release the
    // previous resources before calling (this function only creates).
    // Returns false on failure without leaving a null depth buffer bound to
    // the DSV.
    bool create_depth_resources(
        wz::gpu::dx12::DX12Device* impl,
        UINT width,
        UINT height)
    {
        HRESULT hr;

        // DSV heap (one descriptor — single shared depth buffer).
        if (!impl->dsv_heap)
        {
            D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
            dsv_heap_desc.NumDescriptors = 1;
            dsv_heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            dsv_heap_desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            hr = impl->device->CreateDescriptorHeap(
                &dsv_heap_desc, IID_PPV_ARGS(&impl->dsv_heap));
            if (!wz::gpu::dx12::dx12_check_hr(*impl, hr, "ID3D12Device::CreateDescriptorHeap (DSV)")) {
                return false;
            }
        }

        // Depth resource.
        D3D12_HEAP_PROPERTIES heap_props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_RESOURCE_DESC   res_desc   = CD3DX12_RESOURCE_DESC::Tex2D(
            DEPTH_FORMAT,
            width,
            height,
            1, 1, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

        D3D12_CLEAR_VALUE clear_value{};
        clear_value.Format               = DEPTH_FORMAT;
        clear_value.DepthStencil.Depth   = 1.0f;
        clear_value.DepthStencil.Stencil = 0;

        hr = impl->device->CreateCommittedResource(
            &heap_props,
            D3D12_HEAP_FLAG_NONE,
            &res_desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clear_value,
            IID_PPV_ARGS(&impl->depth_buffer));
        if (!wz::gpu::dx12::dx12_check_hr(*impl, hr, "ID3D12Device::CreateCommittedResource (depth buffer)")) {
            return false;
        }

        // DSV.
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
        dsv_desc.Format        = DEPTH_FORMAT;
        dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsv_desc.Flags         = D3D12_DSV_FLAG_NONE;
        impl->device->CreateDepthStencilView(
            impl->depth_buffer,
            &dsv_desc,
            impl->dsv_heap->GetCPUDescriptorHandleForHeapStart());
        return true;
    }

    void release_depth_buffer(wz::gpu::dx12::DX12Device* impl)
    {
        if (impl->depth_buffer)
        {
            impl->depth_buffer->Release();
            impl->depth_buffer = nullptr;
        }
    }

    bool present_swapchain(
        wz::gpu::dx12::DX12Device* impl,
        uint32_t sync_interval)
    {
        if (!impl || !impl->swapchain) {
            return false;
        }
        if (wz::gpu::dx12::dx12_device_lost(*impl)) {
            return false;
        }

        const HRESULT hr = impl->swapchain->Present(sync_interval, 0);
        if (FAILED(hr)) {
            wz::gpu::dx12::dx12_check_hr(
                *impl,
                hr,
                "IDXGISwapChain::Present");
            return false;
        }
        return true;
    }
}

namespace wz::gpu::dx12
{


    Device create_device(void* native_window)
    {
        HWND hwnd = static_cast<HWND>(native_window);

        // Each Create below is guarded by a real FAILED() check, not an assert:
        // an assert no-ops in release, so a failure (no D3D12 support, exhausted
        // memory) would fall through and null-deref the object that failed to
        // create. Instead we hand back an invalid Device{}, which every caller
        // already checks (device.valid()) to report "no GPU" and skip. On this
        // fatal startup path we let the OS reclaim any objects already created.
        HRESULT hr;
        IDXGIFactory4* factory = nullptr;
        hr = CreateDXGIFactory2(
            0,
            IID_PPV_ARGS(&factory)
        );
        if (FAILED(hr)) {
            return {};
        }

        // Add this before D3D12CreateDevice:
#if defined(_DEBUG)
        {
            ID3D12Debug* debug = nullptr;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
            {
                debug->EnableDebugLayer();
                debug->Release();
            }
        }
#endif

        ID3D12Device* device = nullptr;
        hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
        if (FAILED(hr) || !device) {
            return {};
        }

        // Hold the debug layer's message queue so someone can actually read it
        // (#317). Enabling the layer above without this made every verdict it
        // renders -- unbound vertex buffers, wrong resource states, the
        // depth/DSV mismatch that shipped green in two suites -- reachable only
        // from an attached debugger. QueryInterface fails when the layer is not
        // installed, which is the release build; publish_debug_diagnostics then
        // returns without publishing.
        ID3D12InfoQueue* info_queue = nullptr;
        if (device
            && FAILED(device->QueryInterface(IID_PPV_ARGS(&info_queue))))
        {
            info_queue = nullptr;
        }

        D3D12_COMMAND_QUEUE_DESC qdesc = {};
        qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

        ID3D12CommandQueue* queue = nullptr;
        hr = device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&queue));
        if (FAILED(hr)) {
            return {};
        }

        // ────── create swapchain ───────────────────────────────────────────────────────
        DXGI_SWAP_CHAIN_DESC1 scdesc = {};
        scdesc.BufferCount = 2;
        scdesc.Width = 1280;
        scdesc.Height = 720;
        scdesc.Format = BACKBUFFER_FORMAT; // attention: hardcoded format, must match in resize()
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
        if (FAILED(hr) || !temp) {
            return {};
        }

        // Non-fatal: alt-enter handling only. A failure here must not abort the
        // device, so it is not checked -- unlike the creates around it.
        (void)factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

        IDXGISwapChain3* swapchain = nullptr;
        hr = temp->QueryInterface(IID_PPV_ARGS(&swapchain));
        if (FAILED(hr) || !swapchain) {
            return {};
        }



        factory->Release();
        temp->Release();

        // ────── create rtv heap ───────────────────────────────────────────────────────
        D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
        heap_desc.NumDescriptors = 2;
        heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

        ID3D12DescriptorHeap* rtv_heap = nullptr;
        hr = device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&rtv_heap));
        if (FAILED(hr)) {
            return {};
        }

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

        // ────── command allocators (one per in-flight slot) + list ──────────────
        // #306: one allocator per frame slot so begin_frame resets only the
        // current slot's allocator; the single command list is re-targeted onto it.
        ID3D12CommandAllocator* allocators[DX12Device::kFramesInFlight] = {};
        for (UINT i = 0; i < DX12Device::kFramesInFlight; ++i) {
            hr = device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&allocators[i])
            );
            if (FAILED(hr)) {
                for (UINT j = 0; j < i; ++j) allocators[j]->Release();
                return {};
            }
        }

        ID3D12GraphicsCommandList* cmd = nullptr;
        hr = device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocators[0],
            nullptr,
            IID_PPV_ARGS(&cmd)
        );
        if (FAILED(hr)) {
            for (UINT i = 0; i < DX12Device::kFramesInFlight; ++i) allocators[i]->Release();
            return {};
        }

        hr = cmd->Close();
        if (FAILED(hr)) {
            return {};
        }

        // ────── store everything ───────────────────────────────────────────────────────
        DX12Device* impl = new DX12Device{};
        impl->device = device;
        impl->info_queue = info_queue;
        // Deny INFO/MESSAGE at the source. Without this the layer stores the
        // per-resource-creation chatter too, and the drain allocated a buffer
        // and a string for each one before throwing it away.
        install_debug_storage_filter(impl);
        impl->swapchain = swapchain;
        impl->queue = queue;
        for (UINT i = 0; i < DX12Device::kFramesInFlight; ++i) {
            impl->allocators[i] = allocators[i];
        }
        impl->cmd = cmd;

        // WZ_FRAMES_IN_FLIGHT (1..kFramesInFlight): runtime ring depth. 1 forces
        // serial for an A/B measurement of the frames-in-flight overlap (#306);
        // default is the full ring.
#pragma warning(push)
#pragma warning(disable : 4996)  // std::getenv is the correct, portable call
        if (const char* v = std::getenv("WZ_FRAMES_IN_FLIGHT"); v && v[0]) {
            const int n = std::atoi(v);
            if (n >= 1 && n <= static_cast<int>(DX12Device::kFramesInFlight)) {
                impl->frames_in_flight = static_cast<UINT>(n);
            }
        }
#pragma warning(pop)
        impl->rtv_heap = rtv_heap;
        impl->hwnd = hwnd;
        impl->width = 1280;
        impl->height = 720;

        // ────── scalar fields ───────────────────────────────────────────────
        D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc = {};
        srv_heap_desc.NumDescriptors = 1024;
        srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        hr = device->CreateDescriptorHeap(
            &srv_heap_desc,
            IID_PPV_ARGS(&impl->scalar_field_srv_heap)
        );
        if (FAILED(hr)) {
            delete impl;
            return {};
        }

        impl->scalar_field_srv_stride =
            device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            );

        impl->scalar_field_srv_capacity = 1024;
        impl->scalar_field_srv_count = 0;

        // ────── general SRV/CBV/UAV allocator ──────────────────────────────────────────
        // This one shader-visible heap backs EVERY cached descriptor table the
        // renderer builds (rhi_dx12_command_recorder caches one table per unique
        // (slot, resources) tuple and only releases them on a graph swap), so its
        // capacity must cover the whole live scene at once, not one draw. A single
        // Inochi puppet is the stress case: each Part carries its own vertex+index
        // buffer, so its slot-2 table is a distinct tuple — a 76-Part puppet alone
        // needs 76*3 = 228 descriptors, and the old 256 heap overflowed mid-puppet
        // (create_resource_descriptor_table -> allocate() returned an invalid
        // table, surfacing as "bind_resource_group: slot 2 failed to build a
        // descriptor table"). 16384 (512 KB of descriptors) gives generous room
        // for complex and multiple puppets plus the rest of the scene; a
        // shader-visible CBV/SRV/UAV heap may hold up to 1,000,000 on Tier 1.
        bool srv_alloc_ok = impl->srv_cbv_uav_allocator.init(device, 16384);
        if (!srv_alloc_ok) {
            delete impl;
            return {};
        }

        // ────── initialize fences ───────────────────────────────────────────────────────
        // The frame fence + its event ARE the reclamation timeline (end_frame /
        // wait_for_gpu block on them), so a failed create here must abort the
        // device rather than no-op an assert and leave a null fence/event that a
        // later WaitForSingleObject dereferences.
        hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&impl->fence));
        if (FAILED(hr)) {
            delete impl;
            return {};
        }

        impl->fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        impl->fence_value = 1;

        hr = device->CreateFence(
            0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&impl->copy_fence));
        if (FAILED(hr)) {
            delete impl;
            return {};
        }

        impl->copy_fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        impl->copy_fence_value = 1;

        if (!impl->fence_event || !impl->copy_fence_event) {
            delete impl;
            return {};
        }

        // ────── store backbuffers ───────────────────────────────────────────────────────
        for (UINT i = 0; i < 2; ++i)
        {
            impl->backbuffers[i] = backbuffers[i];
        }

        impl->rtv_stride = rtv_stride;

        // ────── create depth buffer + DSV ────────────────────────────────────
        bool depth_ok = create_depth_resources(impl, impl->width, impl->height);
        if (!depth_ok) {
            delete impl;
            return {};
        }


        // ────── return ───────────────────────────────────────────────────────
        Device out{};
        out.impl = impl;
        return out;
    }

    void create_debug_triangle_opaque_context(
        wz::gpu::Device& device,
        const TriangleTestContextDesc& desc)
    {
        assert(false && "deprecated triangle test path removed");
    }

    void submit_triangle_test_frame(Device& d)
    {
        assert(false && "deprecated triangle test path removed");
    }

    bool begin_frame(Device& d)
    {
        HRESULT hr;
        auto* impl = (DX12Device*)d.impl;
        if (!impl || dx12_device_lost(*impl)) {
            return false;
        }

        impl->frame_index = impl->swapchain->GetCurrentBackBufferIndex();

        // #306 frame-slot ring: advance to this frame's slot and, before reusing
        // its command allocator, wait for the fence the LAST frame in this slot
        // signalled (0 == never used -> no wait). While end_frame still waits on
        // the just-submitted frame this is a no-op (GetCompletedValue is already
        // current); it becomes the real pacing wait once end_frame's wait goes.
        impl->frame_slot = (impl->frame_slot + 1u) % impl->frames_in_flight;
        const UINT64 slot_wait = impl->slot_fence_value[impl->frame_slot];
        if (slot_wait != 0 && impl->fence->GetCompletedValue() < slot_wait) {
            hr = impl->fence->SetEventOnCompletion(slot_wait, impl->fence_event);
            if (!dx12_check_hr(*impl, hr, "ID3D12Fence::SetEventOnCompletion(slot)")) {
                return false;
            }
            if (WaitForSingleObject(impl->fence_event, INFINITE) != WAIT_OBJECT_0) {
                dx12_check_hr(*impl, HRESULT_FROM_WIN32(GetLastError()),
                    "WaitForSingleObject(frame slot)");
                return false;
            }
        }

        ID3D12CommandAllocator* slot_allocator = impl->allocators[impl->frame_slot];

        hr = slot_allocator->Reset();
        if (!dx12_check_hr(*impl, hr, "ID3D12CommandAllocator::Reset")) {
            return false;
        }
        assert(SUCCEEDED(hr));

        hr = impl->cmd->Reset(slot_allocator, nullptr);
        if (!dx12_check_hr(*impl, hr, "ID3D12GraphicsCommandList::Reset")) {
            return false;
        }
        assert(SUCCEEDED(hr));

        // New frame: point each SRV ring at THIS slot's partition of its heap
        // (#306). Each context heap holds kFramesInFlight * kSrvCapacity
        // descriptors; slot s owns [s*cap, (s+1)*cap). The slot-wait above proved
        // the GPU is done with this slot's prior frame, so its descriptors are
        // free to overwrite; another in-flight slot's descriptors are untouched.
        if (impl->blit_ctx) {
            impl->blit_ctx->srv_cursor =
                impl->frame_slot * wz::gpu::dx12::BlitContext::kSrvCapacity;
        }
        if (impl->textured_quad_ctx) {
            impl->textured_quad_ctx->srv_cursor =
                impl->frame_slot * wz::gpu::dx12::TexturedQuadContext::kSrvCapacity;
        }

        // Drain THIS slot's recorded-update staging: the slot-wait above proved
        // the GPU is done with this slot's prior frame, so its staging is free.
        for (ID3D12Resource* staging : impl->frame_upload_staging[impl->frame_slot]) {
            staging->Release();
        }
        impl->frame_upload_staging[impl->frame_slot].clear();
        // impl->cmd->SetGraphicsRootSignature(nullptr); // harmless placeholder sanity reset

        // transition to render target
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = impl->backbuffers[impl->frame_index];
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

        impl->cmd->ResourceBarrier(1, &barrier);

        // ────── RTV + DSV BINDING ───────────────────────────────────────
        auto rtv_handle =
            impl->rtv_heap->GetCPUDescriptorHandleForHeapStart();

        rtv_handle.ptr += impl->frame_index * impl->rtv_stride;

        // DSV is always bound when a depth resource exists.  Render programs
        // that don't want depth simply set DepthMode::Disabled in their
        // pipeline state — the bound DSV is then ignored.
        D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle{};
        D3D12_CPU_DESCRIPTOR_HANDLE* dsv_ptr = nullptr;
        if (impl->dsv_heap)
        {
            dsv_handle = impl->dsv_heap->GetCPUDescriptorHandleForHeapStart();
            dsv_ptr    = &dsv_handle;
        }

        impl->cmd->OMSetRenderTargets(
            1,
            &rtv_handle,
            FALSE,
            dsv_ptr
        );
        impl->depth_target_bound = dsv_ptr != nullptr;
        impl->bound_color_format = BACKBUFFER_FORMAT;


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
        return true;
    }

    void clear(Device& d, float r, float g, float b, float a)
    {
        auto* impl = (DX12Device*)d.impl;
        if (!impl || dx12_device_lost(*impl)) {
            return;
        }

        auto handle = impl->rtv_heap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += impl->frame_index * impl->rtv_stride;

        float color[4] = { r, g, b, a };

        impl->cmd->ClearRenderTargetView(handle, color, 0, nullptr);

        // Also clear the shared depth target (always to 1.0 = far plane).
        // Programs that opt out of depth via DepthMode::Disabled simply
        // ignore this; programs that use TestNoWrite / TestWrite see a
        // freshly-cleared depth each frame.
        if (impl->dsv_heap)
        {
            impl->cmd->ClearDepthStencilView(
                impl->dsv_heap->GetCPUDescriptorHandleForHeapStart(),
                D3D12_CLEAR_FLAG_DEPTH,
                1.0f, 0, 0, nullptr);
        }
    }

    bool end_frame(Device& d)
    {
        HRESULT hr;
        auto* impl = (DX12Device*)d.impl;
        if (!impl || dx12_device_lost(*impl)) {
            return false;
        }

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = impl->backbuffers[impl->frame_index];
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

        impl->cmd->ResourceBarrier(1, &barrier);

        hr = impl->cmd->Close();
        if (!dx12_check_hr(*impl, hr, "ID3D12GraphicsCommandList::Close")) {
            return false;
        }
        assert(SUCCEEDED(hr));

        ID3D12CommandList* lists[] = { impl->cmd };
        impl->queue->ExecuteCommandLists(1, lists);

        // ────── signal the frame fence (no CPU wait -- #306) ────────────────────
        // Submit + signal, then return WITHOUT blocking on the GPU: the CPU is now
        // free to build the next frame while this one renders. The pacing wait
        // moved to begin_frame, which blocks only when the CPU is kFramesInFlight
        // frames ahead (it waits on the slot it is about to reuse). Every per-frame
        // resource is per-slot and fence-gated, so nothing here is recycled early.
        hr = impl->queue->Signal(impl->fence, impl->fence_value);
        if (!dx12_check_hr(*impl, hr, "ID3D12CommandQueue::Signal")) {
            return false;
        }

        // The Signal named this submission on the GPU timeline; spend the value
        // exactly once and record it as THIS slot's fence, so a future begin_frame
        // reusing the slot waits for exactly this frame before recycling it.
        const UINT64 signaled = impl->fence_value++;
        impl->slot_fence_value[impl->frame_slot] = signaled;

        return true;
    }

    bool present(Device& d)
    {
        auto* impl = (DX12Device*)d.impl;
        return present_swapchain(impl, 1);
    }

    bool present(Device& d, uint32_t sync_interval)
    {
        auto* impl = (DX12Device*)d.impl;
        return present_swapchain(impl, sync_interval);
    }

    namespace
    {

        void wait_for_gpu(DX12Device* impl)
        {
            HRESULT hr;
            if (!impl || dx12_device_lost(*impl)) {
                return;
            }

            hr = impl->queue->Signal(impl->fence, impl->fence_value);
            if (!dx12_check_hr(*impl, hr, "ID3D12CommandQueue::Signal")) {
                return;
            }

            // Advance the fence so every value is signaled exactly ONCE, the
            // moment the Signal succeeds -- before the wait, so a wait-setup
            // failure below cannot leave the value live for the next Signal to
            // reuse. Without single-use, end_frame's wait could short-circuit
            // (GetCompletedValue() already >= that value), transiently allowing a
            // frame in flight under a value that no longer identifies one
            // submission -- and the registry's reclamation, which judges a
            // resource complete by its touch value, would then be wrong.
            const UINT64 signaled = impl->fence_value++;

            // If GPU hasn't reached this fence value yet → wait
            if (impl->fence->GetCompletedValue() < signaled)
            {
                hr = impl->fence->SetEventOnCompletion(signaled, impl->fence_event);
                if (!dx12_check_hr(
                        *impl,
                        hr,
                        "ID3D12Fence::SetEventOnCompletion"))
                {
                    return;
                }

                if (WaitForSingleObject(impl->fence_event, INFINITE)
                    != WAIT_OBJECT_0)
                {
                    dx12_check_hr(*impl, HRESULT_FROM_WIN32(GetLastError()),
                        "WaitForSingleObject(wait_for_gpu)");
                }
            }
        }

    }

    void wait_idle(Device& d)
    {
        auto* impl = static_cast<DX12Device*>(d.impl);
        if (!impl || !impl->queue || !impl->fence || !impl->fence_event)
            return;
        if (dx12_device_lost(*impl))
            return;

        wait_for_gpu(impl);
    }

    bool copy_queue_wait(dx12::DX12Device* impl)
    {
        if (!impl || !impl->queue || !impl->copy_fence
            || !impl->copy_fence_event)
        {
            return false;
        }
        if (dx12::dx12_device_lost(*impl)) {
            return false;
        }

        // The copy fence, never the frame fence: the frame fence's values are
        // the reclamation timeline, and only end_frame may signal those (see
        // DX12Device::copy_fence).
        HRESULT hr = impl->queue->Signal(
            impl->copy_fence, impl->copy_fence_value);
        if (!dx12::dx12_check_hr(
                *impl, hr, "ID3D12CommandQueue::Signal(copy)"))
        {
            return false;
        }

        // Signal succeeded: spend this copy-fence value exactly once, so a
        // wait-setup failure below cannot leave it live for the next upload's
        // Signal to reuse (which could then short-circuit its wait and read a
        // resource before its copy landed).
        const UINT64 signaled = impl->copy_fence_value++;

        if (impl->copy_fence->GetCompletedValue() < signaled) {
            hr = impl->copy_fence->SetEventOnCompletion(
                signaled, impl->copy_fence_event);
            if (!dx12::dx12_check_hr(
                    *impl, hr, "ID3D12Fence::SetEventOnCompletion(copy)"))
            {
                return false;
            }
            if (WaitForSingleObject(impl->copy_fence_event, INFINITE)
                != WAIT_OBJECT_0)
            {
                return false;
            }
        }

        return true;
    }

    uint64_t frame_timeline_value(Device& d)
    {
        auto* impl = static_cast<DX12Device*>(d.impl);
        if (!impl || dx12_device_lost(*impl))
            return 0;
        // The value end_frame will Signal for the frame currently being
        // recorded. Every submission signals this value exactly once (see
        // wait_for_gpu), so it uniquely identifies this frame's GPU work — the
        // value to touch resources with for precise reclamation.
        return impl->fence_value;
    }

    uint64_t completed_timeline_value(Device& d)
    {
        auto* impl = static_cast<DX12Device*>(d.impl);
        if (!impl || !impl->fence)
            return 0;
        // On a removed device GetCompletedValue returns UINT64_MAX, which would
        // signal "every timeline value is complete" and make collect reclaim
        // resources the (dead) GPU may still notionally reference. Device-loss
        // cleanup is owned by GpuResourceRegistry::on_device_lost, so report 0
        // here instead of letting the sentinel leak into collect().
        if (dx12_device_lost(*impl))
            return 0;
        return impl->fence->GetCompletedValue();
    }

    void destroy_device(Device& d)
    {
        auto* impl = (DX12Device*)d.impl;
        if (!impl) return;

        if (!dx12_device_lost(*impl)) {
            wait_for_gpu(impl);
        }

        // Recorded-update staging from every in-flight slot (all waited-for by
        // wait_for_gpu above).
        for (auto& slot_staging : impl->frame_upload_staging) {
            for (ID3D12Resource* staging : slot_staging) {
                staging->Release();
            }
            slot_staging.clear();
        }

        // 1. Destroy GPU resource tables.
        impl->compute_pipelines.destroy();
        impl->compute_buffers.destroy();
        impl->textures.destroy();
        impl->graphics_pipelines.destroy();
        impl->shaders.destroy();
        impl->meshes.destroy();
        impl->mesh_field_visualizations.destroy(
            impl->srv_cbv_uav_allocator);
        impl->srv_cbv_uav_allocator.destroy();

        if (impl->scalar_debug_ctx)
        {
            if (impl->scalar_debug_ctx->pso)
            {
                impl->scalar_debug_ctx->pso->Release();
                impl->scalar_debug_ctx->pso = nullptr;
            }

            if (impl->scalar_debug_ctx->root_sig)
            {
                impl->scalar_debug_ctx->root_sig->Release();
                impl->scalar_debug_ctx->root_sig = nullptr;
            }

            delete impl->scalar_debug_ctx;
            impl->scalar_debug_ctx = nullptr;
        }
        if (impl->blit_ctx) {
            if (impl->blit_ctx->pso) {
                impl->blit_ctx->pso->Release();
            }
            if (impl->blit_ctx->pso_srgb_encode) {
                impl->blit_ctx->pso_srgb_encode->Release();
            }
            if (impl->blit_ctx->root_sig) {
                impl->blit_ctx->root_sig->Release();
            }
            if (impl->blit_ctx->srv_heap) {
                impl->blit_ctx->srv_heap->Release();
            }
            delete impl->blit_ctx;
            impl->blit_ctx = nullptr;
        }
        if (impl->textured_quad_ctx) {
            if (impl->textured_quad_ctx->pso) {
                impl->textured_quad_ctx->pso->Release();
            }
            if (impl->textured_quad_ctx->pso_world) {
                impl->textured_quad_ctx->pso_world->Release();
            }
            if (impl->textured_quad_ctx->pso_composite) {
                impl->textured_quad_ctx->pso_composite->Release();
            }
            if (impl->textured_quad_ctx->root_sig) {
                impl->textured_quad_ctx->root_sig->Release();
            }
            if (impl->textured_quad_ctx->srv_heap) {
                impl->textured_quad_ctx->srv_heap->Release();
            }
            delete impl->textured_quad_ctx;
            impl->textured_quad_ctx = nullptr;
        }
        impl->scalar_field_textures.destroy();

        if (impl->scalar_field_srv_heap) {
            impl->scalar_field_srv_heap->Release();
            impl->scalar_field_srv_heap = nullptr;
        }

        if (impl->mesh_wire_debug_ctx)
        {
            if (impl->mesh_wire_debug_ctx->pso)
            {
                impl->mesh_wire_debug_ctx->pso->Release();
                impl->mesh_wire_debug_ctx->pso = nullptr;
            }

            if (impl->mesh_wire_debug_ctx->root_sig)
            {
                impl->mesh_wire_debug_ctx->root_sig->Release();
                impl->mesh_wire_debug_ctx->root_sig = nullptr;
            }

            delete impl->mesh_wire_debug_ctx;
            impl->mesh_wire_debug_ctx = nullptr;
        }

        // 3. Release swapchain/backbuffer resources.
        for (int i = 0; i < 2; ++i)
        {
            if (impl->backbuffers[i])
            {
                impl->backbuffers[i]->Release();
                impl->backbuffers[i] = nullptr;
            }
        }

        release_depth_buffer(impl);
        if (impl->dsv_heap) { impl->dsv_heap->Release(); impl->dsv_heap = nullptr; }
        if (impl->rtv_heap) { impl->rtv_heap->Release();  impl->rtv_heap = nullptr; }
        if (impl->cmd) { impl->cmd->Release();       impl->cmd = nullptr; }
        for (UINT i = 0; i < DX12Device::kFramesInFlight; ++i) {
            if (impl->allocators[i]) { impl->allocators[i]->Release(); impl->allocators[i] = nullptr; }
        }
        if (impl->swapchain) { impl->swapchain->Release(); impl->swapchain = nullptr; }
        if (impl->queue) { impl->queue->Release();     impl->queue = nullptr; }
        // Before the device: the info queue holds a reference to it, so
        // releasing it later would keep the device alive past its own release
        // and turn the shutdown report into a false live-object leak.
        if (impl->info_queue) { impl->info_queue->Release(); impl->info_queue = nullptr; }

        if (impl->fence) { impl->fence->Release(); impl->fence = nullptr; }
        if (impl->fence_event) { CloseHandle(impl->fence_event); impl->fence_event = nullptr; }
        if (impl->copy_fence) { impl->copy_fence->Release(); impl->copy_fence = nullptr; }
        if (impl->copy_fence_event) { CloseHandle(impl->copy_fence_event); impl->copy_fence_event = nullptr; }

#if defined(_DEBUG)
        if (impl->device)
        {
            ID3D12DebugDevice* debug_device = nullptr;
            if (SUCCEEDED(impl->device->QueryInterface(IID_PPV_ARGS(&debug_device))))
            {
                impl->device->Release();
                impl->device = nullptr;

                debug_device->ReportLiveDeviceObjects(
                    D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL
                );

                debug_device->Release();
            }
        }
#endif

        if (impl->device)
        {
            impl->device->Release();
            impl->device = nullptr;
        }

        delete impl;
        d.impl = nullptr;
    }

    // ────── resize ───────────────────────────────────────────────────────

    bool resize(Device& d, int w, int h)
    {
        auto* impl = (DX12Device*)d.impl;
        if (!impl || !impl->swapchain)
            return false;
        if (dx12_device_lost(*impl))
            return false;

        // Minimized windows deliver WM_SIZE(0,0): a non-positive extent is a
        // benign no-op.  Leave impl->width/height and the swapchain untouched
        // so the old swapchain stays valid until a non-zero size arrives.
        if (w <= 0 || h <= 0)
            return true;

        impl->width = w;
        impl->height = h;

        // 1. ensure GPU is idle
        wait_for_gpu(impl);

        // 2. release current backbuffers + depth buffer
        for (int i = 0; i < 2; ++i)
        {
            if (impl->backbuffers[i])
            {
                impl->backbuffers[i]->Release();
                impl->backbuffers[i] = nullptr;
            }
        }
        release_depth_buffer(impl);

        // 3. resize swapchain buffers
        HRESULT hr = impl->swapchain->ResizeBuffers(
            2,
            w,
            h,
            BACKBUFFER_FORMAT,// attention: hardcoded format
            0
        );
        if (!dx12_check_hr(*impl, hr, "IDXGISwapChain::ResizeBuffers")) {
            return false;
        }
        assert(SUCCEEDED(hr));

        // 4. reacquire backbuffers
        auto handle = impl->rtv_heap->GetCPUDescriptorHandleForHeapStart();

        for (UINT i = 0; i < 2; ++i)
        {
            hr = impl->swapchain->GetBuffer(i, IID_PPV_ARGS(&impl->backbuffers[i]));
            if (!dx12_check_hr(*impl, hr, "IDXGISwapChain::GetBuffer")) {
                return false;
            }
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

        // 5. recreate depth buffer at the new size
        if (!create_depth_resources(impl, static_cast<UINT>(w), static_cast<UINT>(h))) {
            return false;
        }
        return !dx12_device_lost(*impl);
    }

    wz::gpu::DeviceStatus device_status(const wz::gpu::Device& device)
    {
        return dx12_device_status(static_cast<const DX12Device*>(device.impl));
    }

    const wz::gpu::DeviceLostInfo* device_lost_info(
        const wz::gpu::Device& device)
    {
        return dx12_device_lost_info(
            static_cast<const DX12Device*>(device.impl));
    }


} // namespace wz::gpu::dx12

namespace wz::gpu::dx12::internal
{   // file: src/gpu/dx12/dx12_device.cpp

    D3D12_CPU_DESCRIPTOR_HANDLE get_current_rtv(Device& d)
    {
        auto* impl = static_cast<DX12Device*>(d.impl);
        assert(impl);

        D3D12_CPU_DESCRIPTOR_HANDLE handle =
            impl->rtv_heap->GetCPUDescriptorHandleForHeapStart();

        handle.ptr += impl->frame_index * impl->rtv_stride;

        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE get_dsv(Device& d)
    {
        auto* impl = static_cast<DX12Device*>(d.impl);
        assert(impl && impl->dsv_heap);
        return impl->dsv_heap->GetCPUDescriptorHandleForHeapStart();
    }

    UINT get_width(Device& d)
    {
        auto* impl = static_cast<DX12Device*>(d.impl);
        assert(impl);
        return impl->width;
    }

    UINT get_height(Device& d)
    {
        auto* impl = static_cast<DX12Device*>(d.impl);
        assert(impl);
        return impl->height;
    }

    // Offscreen render-to-texture (S6). Begin: transition the render-target texture
    // to RENDER_TARGET, bind it as the sole color target at its own dimensions
    // (no depth -- the overlay/puppet path is depth-Disabled), and clear it. Draws
    // recorded after this land in the texture. Must be inside a begin_frame/
    // end_frame bracket (uses the device's open command list). `clear_color` may be
    // null to skip the clear.
    bool begin_offscreen_pass(Device& d, GPUHandle rt, const float clear_color[4])
    {
        auto* impl = static_cast<DX12Device*>(d.impl);
        if (!impl || !impl->cmd) {
            return false;
        }
        DX12Texture* tex = impl->textures.get(rt);
        if (!tex || !tex->valid() || !tex->is_render_target || !tex->rtv_heap) {
            return false;
        }
        if (!transition_texture_to_render_target_dx12(d, rt)) {
            return false;
        }
        // No depth target: the overlay/puppet path is depth-disabled. A dedicated
        // offscreen depth buffer is a follow-up for depth-tested content.
        impl->cmd->OMSetRenderTargets(1, &tex->rtv, FALSE, nullptr);
        impl->depth_target_bound = false;
        impl->bound_color_format = tex->format;
        D3D12_VIEWPORT vp{};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = static_cast<float>(tex->width);
        vp.Height = static_cast<float>(tex->height);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        D3D12_RECT sc{ 0, 0,
            static_cast<LONG>(tex->width), static_cast<LONG>(tex->height) };
        impl->cmd->RSSetViewports(1, &vp);
        impl->cmd->RSSetScissorRects(1, &sc);
        if (clear_color) {
            impl->cmd->ClearRenderTargetView(tex->rtv, clear_color, 0, nullptr);
        }
        return true;
    }

    // End an offscreen pass: transition the texture back to shader-read (so it can
    // be sampled) and rebind the backbuffer + its viewport so any subsequent draws
    // this frame hit the screen.
    bool end_offscreen_pass(Device& d, GPUHandle rt)
    {
        auto* impl = static_cast<DX12Device*>(d.impl);
        if (!impl || !impl->cmd) {
            return false;
        }
        if (!transition_texture_to_shader_read_dx12(d, rt)) {
            return false;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = get_current_rtv(d);
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = get_dsv(d);
        impl->cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
        impl->depth_target_bound = true;
        impl->bound_color_format = BACKBUFFER_FORMAT;
        D3D12_VIEWPORT vp{};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = static_cast<float>(get_width(d));
        vp.Height = static_cast<float>(get_height(d));
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        D3D12_RECT sc{ 0, 0,
            static_cast<LONG>(get_width(d)), static_cast<LONG>(get_height(d)) };
        impl->cmd->RSSetViewports(1, &vp);
        impl->cmd->RSSetScissorRects(1, &sc);
        return true;
    }

    // Bind a full-screen colour target as the primary pass's RTV WITH the shared
    // depth buffer -- unlike begin_offscreen_pass, which binds no depth for the
    // mask/overlay path. The scene renders depth-tested into this (an RGBA16F
    // linear target) instead of the backbuffer, so lighting accumulates in
    // linear space; a later encode resolves it to the sRGB backbuffer (#324).
    // Transitions the target to RENDER_TARGET, binds target+DSV, records the
    // bound colour format for the PSO key, viewport = target dims, clears both.
    bool begin_primary_color_pass(Device& d, GPUHandle color_target,
                                  const float clear_color[4])
    {
        auto* impl = static_cast<DX12Device*>(d.impl);
        if (!impl || !impl->cmd) {
            return false;
        }
        DX12Texture* tex = impl->textures.get(color_target);
        if (!tex || !tex->valid() || !tex->is_render_target || !tex->rtv_heap) {
            return false;
        }
        if (!transition_texture_to_render_target_dx12(d, color_target)) {
            return false;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
        D3D12_CPU_DESCRIPTOR_HANDLE* dsv_ptr = nullptr;
        if (impl->dsv_heap) {
            dsv = impl->dsv_heap->GetCPUDescriptorHandleForHeapStart();
            dsv_ptr = &dsv;
        }
        impl->cmd->OMSetRenderTargets(1, &tex->rtv, FALSE, dsv_ptr);
        impl->depth_target_bound = dsv_ptr != nullptr;
        impl->bound_color_format = tex->format;
        D3D12_VIEWPORT vp{ 0.0f, 0.0f,
            static_cast<float>(tex->width), static_cast<float>(tex->height),
            0.0f, 1.0f };
        D3D12_RECT sc{ 0, 0,
            static_cast<LONG>(tex->width), static_cast<LONG>(tex->height) };
        impl->cmd->RSSetViewports(1, &vp);
        impl->cmd->RSSetScissorRects(1, &sc);
        if (clear_color) {
            impl->cmd->ClearRenderTargetView(tex->rtv, clear_color, 0, nullptr);
        }
        if (dsv_ptr) {
            impl->cmd->ClearDepthStencilView(
                *dsv_ptr, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        }
        return true;
    }

    // Close a begin_primary_color_pass: transition the colour target to
    // shader-read (so the encode pass can sample it) and rebind the backbuffer +
    // its viewport for the rest of the frame (encode, overlays, present). Mirror
    // of end_offscreen_pass, kept separate so its intent reads at the call site.
    bool end_primary_color_pass(Device& d, GPUHandle color_target)
    {
        auto* impl = static_cast<DX12Device*>(d.impl);
        if (!impl || !impl->cmd) {
            return false;
        }
        if (!transition_texture_to_shader_read_dx12(d, color_target)) {
            return false;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = get_current_rtv(d);
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = get_dsv(d);
        impl->cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
        impl->depth_target_bound = true;
        impl->bound_color_format = BACKBUFFER_FORMAT;
        D3D12_VIEWPORT vp{ 0.0f, 0.0f,
            static_cast<float>(get_width(d)), static_cast<float>(get_height(d)),
            0.0f, 1.0f };
        D3D12_RECT sc{ 0, 0,
            static_cast<LONG>(get_width(d)), static_cast<LONG>(get_height(d)) };
        impl->cmd->RSSetViewports(1, &vp);
        impl->cmd->RSSetScissorRects(1, &sc);
        return true;
    }


    GPUHandle store_shader(
        Device& d,
        ID3DBlob* blob,
        wz::gpu::ShaderStage stage)
    {
        auto* impl = (wz::gpu::dx12::DX12Device*)d.impl;
        assert(impl);
        assert(blob);

        return impl->shaders.add(blob, stage);
    }

    const DX12Shader* get_shader(
        wz::gpu::Device& d,
        wz::gpu::GPUHandle handle)
    {
        auto* impl = (wz::gpu::dx12::DX12Device*)d.impl;
        assert(impl);

        return impl->shaders.get(handle);
    }


    ID3D12Device* get_device(Device& d)
    {
        auto* impl = (DX12Device*)d.impl;
        assert(impl);
        return impl->device;
    }

    bool depth_target_bound(Device& d)
    {
        auto* impl = (DX12Device*)d.impl;
        return impl && impl->depth_target_bound;
    }

    void set_depth_target_bound(Device& d, bool bound)
    {
        if (auto* impl = (DX12Device*)d.impl) {
            impl->depth_target_bound = bound;
        }
    }

    DXGI_FORMAT bound_color_format(Device& d)
    {
        auto* impl = (DX12Device*)d.impl;
        return impl ? impl->bound_color_format : DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    void set_bound_color_format(Device& d, DXGI_FORMAT format)
    {
        if (auto* impl = (DX12Device*)d.impl) {
            impl->bound_color_format = format;
        }
    }

    void publish_debug_diagnostics(Device& d, uint16_t pass)
    {
        auto* impl = (DX12Device*)d.impl;
        if (!impl || !impl->info_queue) {
            return;  // release build (no debug layer) -- one null check
        }

        ID3D12InfoQueue* q = impl->info_queue;
        const UINT64 count = q->GetNumStoredMessages();
        if (count == 0) {
            return;  // nothing stored for this pass
        }

        wz::diag::IDiagnosticSink* sink = wz::diag::get_diagnostic_sink();
        if (!sink) {
            // No LoggerService lane installed -- nowhere to publish. Still clear the
            // queue so a stored message never wedges GetNumStoredMessages() nonzero.
            q->ClearStoredMessages();
            return;
        }

        // Per-drain dedup by id: fold repeats of one id in THIS drain into a single
        // record carrying the occurrence count, so a burst of the same warning
        // crosses the channel once (the lane accumulates the total across drains).
        // Fixed stack table -- no allocation; distinct debug ids in one pass are few.
        struct DrainEntry
        {
            D3D12_MESSAGE_ID       id;
            uint32_t               occurrences;
            D3D12_MESSAGE_SEVERITY severity;
            uint8_t                category;
        };
        constexpr size_t kDrainCap = 64;
        DrainEntry entries[kDrainCap];
        size_t     distinct = 0;

        auto publish_one = [&](const DrainEntry& e) {
            wz::diag::DiagnosticRecord r;
            r.id          = static_cast<uint32_t>(e.id);
            r.occurrences = e.occurrences;
            r.severity    = to_diagnostic_severity(e.severity);
            r.source      = wz::diag::DiagnosticSource::D3D12;
            r.category    = e.category;
            r.pass        = pass;
            sink->publish(r);  // non-blocking; drops on a full channel
        };

        for (UINT64 i = 0; i < count; ++i) {
            SIZE_T length = 0;
            if (FAILED(q->GetMessage(i, nullptr, &length)) || length == 0) {
                continue;
            }
            if (impl->debug_scratch.size() < length) {
                impl->debug_scratch.resize(length);
            }
            auto* message =
                reinterpret_cast<D3D12_MESSAGE*>(impl->debug_scratch.data());
            if (FAILED(q->GetMessage(i, message, &length))) {
                continue;
            }
            // Only the three severities that mean "this frame is wrong". INFO/
            // MESSAGE are already denied at storage; this guards the drain anyway.
            // No string built from pDescription -- id/severity/category are enough,
            // and the description would be hot-lane allocation (#291).
            if (message->Severity != D3D12_MESSAGE_SEVERITY_CORRUPTION
                && message->Severity != D3D12_MESSAGE_SEVERITY_ERROR
                && message->Severity != D3D12_MESSAGE_SEVERITY_WARNING) {
                continue;
            }

            DrainEntry* slot = nullptr;
            for (size_t j = 0; j < distinct; ++j) {
                if (entries[j].id == message->ID) {
                    slot = &entries[j];
                    break;
                }
            }
            if (slot) {
                ++slot->occurrences;
                slot->severity = message->Severity;  // stable per id
                slot->category = static_cast<uint8_t>(message->Category);
            }
            else if (distinct < kDrainCap) {
                entries[distinct++] = {
                    message->ID, 1u, message->Severity,
                    static_cast<uint8_t>(message->Category) };
            }
            else {
                // More distinct ids in one drain than the fold table holds
                // (pathological) -- publish this occurrence directly, never drop it.
                publish_one({ message->ID, 1u, message->Severity,
                              static_cast<uint8_t>(message->Category) });
            }
        }

        for (size_t j = 0; j < distinct; ++j) {
            publish_one(entries[j]);
        }

        q->ClearStoredMessages();
    }

    ID3D12GraphicsCommandList* get_command_list(Device& d)
    {
        auto* impl = (DX12Device*)d.impl;
        assert(impl);
        return impl->cmd;
    }

    ID3D12DescriptorHeap* get_srv_cbv_uav_heap(Device& d)
    {
        auto* impl = static_cast<DX12Device*>(d.impl);
        if (!impl) return nullptr;
        return impl->srv_cbv_uav_allocator.heap();
    }

    ID3D12RootSignature* create_empty_root_signature(ID3D12Device* device)
    {
        D3D12_ROOT_PARAMETER param = {};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        param.Constants.Num32BitValues = 40;
        param.Constants.RegisterSpace = 0;
        param.Constants.ShaderRegister = 0;
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters = 1;
        desc.pParameters = &param;
        desc.NumStaticSamplers = 0;
        desc.pStaticSamplers = nullptr;
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ID3DBlob* sig_blob = nullptr;
        ID3DBlob* error_blob = nullptr;

        HRESULT hr = D3D12SerializeRootSignature(
            &desc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &sig_blob,
            &error_blob
        );

        if (FAILED(hr))
        {
            if (error_blob)
            {
                OutputDebugStringA(
                    static_cast<const char*>(error_blob->GetBufferPointer())
                );
                error_blob->Release();
            }

            assert(false);
            return nullptr;
        }

        ID3D12RootSignature* root_sig = nullptr;

        hr = device->CreateRootSignature(
            0,
            sig_blob->GetBufferPointer(),
            sig_blob->GetBufferSize(),
            IID_PPV_ARGS(&root_sig)
        );

        assert(SUCCEEDED(hr));

        sig_blob->Release();
        if (error_blob) error_blob->Release();

        return root_sig;
    }

    extern const BYTE g_VS[];
    //extern const SIZE_T g_VS_size;

    extern const BYTE g_PS[];
    extern const SIZE_T g_PS_size;


    ID3D12PipelineState* create_triangle_pso(
        wz::gpu::Device& device,
        ID3D12RootSignature* root_sig,
        wz::gpu::GPUHandle vertex_shader,
        wz::gpu::GPUHandle pixel_shader)
    {
        const DX12Shader* vs = get_shader(device, vertex_shader);
        const DX12Shader* ps = get_shader(device, pixel_shader);

        assert(vs);
        assert(ps);
        assert(vs->blob);
        assert(ps->blob);
        assert(vs->stage == ShaderStage::Vertex);
        assert(ps->stage == ShaderStage::Pixel);

        D3D12_INPUT_ELEMENT_DESC layout[] =
        {
            {
                "POSITION",
                0,
                DXGI_FORMAT_R32G32B32_FLOAT,
                0,
                0,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                0
            }
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = root_sig;

        desc.VS = {
            vs->blob->GetBufferPointer(),
            vs->blob->GetBufferSize()
        };

        desc.PS = {
            ps->blob->GetBufferPointer(),
            ps->blob->GetBufferSize()
        };

        desc.InputLayout = { layout, 1 };
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.DSVFormat = DXGI_FORMAT_UNKNOWN;

        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = BACKBUFFER_FORMAT;

        desc.SampleMask = UINT_MAX;
        desc.SampleDesc.Count = 1;

        ID3D12PipelineState* pso = nullptr;
        HRESULT hr = get_device(device)->CreateGraphicsPipelineState(
            &desc,
            IID_PPV_ARGS(&pso)
        );

        if (FAILED(hr))
        {
            char buf[256];
            sprintf_s(buf, "CreateGraphicsPipelineState failed: 0x%08X\n", (unsigned)hr);
            OutputDebugStringA(buf);
            return nullptr;
        }

        return pso;
    }

    // The one backbuffer fact the engine still needs: every PSO that targets
    // the swapchain declares this format. NOT _SRGB -- see #327, which puts
    // post-processing below its own R1 for exactly this reason.
    DXGI_FORMAT get_backbuffer_format()
    {
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
}
