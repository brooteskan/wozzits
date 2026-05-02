// file: src/gpu/dx12/dx12_device.cpp

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <gpu/dx12/external/d3dx12.h>
#include <engine/render_backends/dx12/dx12_submit.h>
#include <wrl/client.h>

#include <gpu/gpu.h>
#include <gpu/dx12/dx12_internal.h>
#include <window/window2.h>
#include <cassert>


#include <render/frame/render_frame.h>
#include <scene/scene_graph.h>
#include <render/ir/render_ir.h>
#include <scene/compile/scene_compiler.h>
#include <render/frame/render_frame.h>

#include <iostream>
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")




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

        wz::render::backend::dx12::Context* ctx = nullptr;
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
        impl->ctx = wz::render::backend::dx12::create(out);
        return out;
    }

    void draw_test_triangle(Device& d)
    {
        // this was Day 2
        auto* impl = (DX12Device*)d.impl;

        ID3D12GraphicsCommandList* cmd = impl->cmd;

        auto* ctx = impl->ctx; // assuming you still have it stored

        assert(ctx->pso != nullptr);
        assert(ctx->root_sig != nullptr);
        assert(impl->cmd != nullptr);

        cmd->SetGraphicsRootSignature(ctx->root_sig);
        cmd->SetPipelineState(ctx->pso);

        assert(impl);
        assert(impl->cmd);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->IASetVertexBuffers(0, 1, &ctx->vb_view);

        cmd->DrawInstanced(3, 1, 0, 0);
    }


    using namespace wz::scene;
    using namespace wz::core::graph;
    using namespace wz::math;

    Mat4 translation_x(float z)
    {
        Mat4 m = mat4_identity();
        m.m[12] = z;
        return m;
    }
    SceneStorage build_test_scene()
    {
        SceneBuilder b;

        TransformNode root{};
        root.local = mat4_identity();
        auto root_h = add_node(b, root);

        TransformNode node{};
        node.local = translation_x( -0.8f); // no transform yet
        node.flags = TransformNodeFlag::RenderDomain;

        auto h = add_node(b, node);
        add_edge(b, root_h, h);

        auto result = build(std::move(b));
        assert(result.has_value());

        return std::move(*result);
    }


    std::vector<RenderableDescriptor> build_descriptors()
    {
        std::vector<RenderableDescriptor> descs(2);

        descs[0] = { RenderPipeline::None };

        descs[1] = {
            RenderPipeline::OpaqueGeometry,
            /*mesh=*/0,
            /*material=*/0,
            {}
        };

        return descs;
    }

    Mat4 perspective(float fov, float aspect, float nearZ, float farZ)
    {
        float f = 1.0f / tanf(fov * 0.5f);

        Mat4 m = {};

        m.m[0] = f / aspect;
        m.m[5] = f;

        m.m[10] = farZ / (farZ - nearZ);
        m.m[11] = 1.0f;

        m.m[14] = (-nearZ * farZ) / (farZ - nearZ);

        return m;
    }

    ViewData make_camera()
    {
        ViewData v{};

        v.view = mat4_identity();
        v.view = translation_x(1.0f);

        float fov = 60.0f * 3.14159265f / 180.0f;
        float aspect = 1280.0f / 720.0f;
        float nearZ = 0.1f;
        float farZ = 100.0f;

        // v.projection = perspective(fov, aspect, nearZ, farZ);
        v.projection = mat4_identity();
        v.view_projection = mul(v.projection,v.view); // IMPORTANT ORDER
        v.camera_position = Vec3{ 1.5,0,0 };

        return v;
    }

    void draw_test_triangle_2(Device& d)
    {
        // We are at Day 3 now.
        auto* impl = (DX12Device*)d.impl;
        auto* ctx = impl->ctx;

        // ────── build scene ─────────────────────────────
        auto scene = build_test_scene();
        auto descs = build_descriptors();
        auto view = make_camera();
        ctx->view_proj = view.view_projection;

        propagate_all(scene.polytree);

        auto compiled = compile(scene.polytree, descs, {}, view);

        // ────── IR → Frame ───────────────────────────────
        using namespace wz::render;
        auto ir = build_render_ir(compiled.scene);
        RenderFrameStorage storage = build_frame(ir, compiled.scene);

        if (ctx->mesh_table.empty())
        {
            // TEMP: bootstrap one GPU mesh for mesh=0

            struct Vertex { float x, y, z; };
            Vertex tri[3] = {
                { 0.0f,  0.5f, 0.0f },
                { 0.5f, -0.5f, 0.0f },
                {-0.5f, -0.5f, 0.0f }
            };

            auto* dev = wz::gpu::dx12::internal::get_device(*ctx->device);

            ID3D12Resource* vb = nullptr;

            D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_UPLOAD };
            D3D12_RESOURCE_DESC desc = {};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = sizeof(tri);
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            dev->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr, IID_PPV_ARGS(&vb));

            void* mapped;
            vb->Map(0, nullptr, &mapped);
            memcpy(mapped, tri, sizeof(tri));
            vb->Unmap(0, nullptr);

            wz::render::backend::dx12::GpuMesh m{};
            m.vertex_buffer = vb;
            m.vb_view.BufferLocation = vb->GetGPUVirtualAddress();
            m.vb_view.StrideInBytes = sizeof(Vertex);
            m.vb_view.SizeInBytes = sizeof(tri);
            m.index_buffer = nullptr;
            m.index_count = 3;

            ctx->mesh_table.push_back(m);
        }

        // ────── submit (IMPORTANT: unwrap frame) ─────────
        wz::render::backend::dx12::submit(ctx, storage.frame);
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
        // impl->cmd->SetGraphicsRootSignature(nullptr); // harmless placeholder sanity reset

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

        if (impl->ctx)
        {
            wz::render::backend::dx12::destroy(impl->ctx);
            impl->ctx = nullptr;
        }

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

namespace wz::gpu::dx12::internal
{   // file: src/gpu/dx12/dx12_device.cpp
    namespace wz::gpu::dx12::internal
    {
        DX12Device* get_backend(Device& d)
        {
            auto* impl = (DX12Device*)d.impl;
            assert(impl);
            return impl;
        }
    }

    ID3D12Device* get_device(Device& d)
    {
        auto* impl = (DX12Device*)d.impl;
        assert(impl);
        return impl->device;
    }

    ID3D12GraphicsCommandList* get_command_list(Device& d)
    {
        auto* impl = (DX12Device*)d.impl;
        assert(impl);
        return impl->cmd;
    }

    ID3D12RootSignature* create_empty_root_signature(ID3D12Device* device)
    {
        D3D12_ROOT_PARAMETER param = {};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        param.Constants.Num32BitValues = 32; // 4x4 matrix
        param.Constants.RegisterSpace = 0;
        param.Constants.ShaderRegister = 0;
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

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

        assert(SUCCEEDED(hr));

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
        ID3D12Device* device,
        ID3D12RootSignature* root_sig)
    {
        HRESULT hr;

        // ────── compile vertex shader ──────
        ID3DBlob* vs = nullptr;
        ID3DBlob* ps = nullptr;
        ID3DBlob* error = nullptr;

        const char* vs_src = R"(
            cbuffer Transform : register(b0)
            {
                float4x4 world;
                float4x4 view_proj;
            };

            struct VSOut {
                float4 pos : SV_POSITION;
            };

            VSOut main(float3 pos : POSITION)
            {
                VSOut o;

                float4 p = float4(pos, 1.0);
                p = mul(world, p);
                o.pos = mul(view_proj, p);

                return o;
            }
            )";

        hr = D3DCompile(
            vs_src, strlen(vs_src),
            nullptr, nullptr, nullptr,
            "main", "vs_5_0",
            0, 0,
            &vs, &error
        );
        assert(SUCCEEDED(hr));

        // ────── compile pixel shader ──────
        const char* ps_src = R"(
        float4 main() : SV_TARGET
        {
            return float4(1, 0, 0, 1);
        }
    )";

        hr = D3DCompile(
            ps_src, strlen(ps_src),
            nullptr, nullptr, nullptr,
            "main", "ps_5_0",
            0, 0,
            &ps, &error
        );
        assert(SUCCEEDED(hr));

        // ────── input layout ──────
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

        // ────── PSO desc ──────
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = root_sig;

        desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
        desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };

        desc.InputLayout = { layout, 1 };
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

        desc.SampleMask = UINT_MAX;
        desc.SampleDesc.Count = 1;

        ID3D12PipelineState* pso = nullptr;
        hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso));
        assert(SUCCEEDED(hr));

        vs->Release();
        ps->Release();

        return pso;
    }
}