#pragma once
// src/gpu/dx12/dx12_device_internal.h


#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <gpu/dx12/dx12_shader.h>
#include <gpu/dx12/dx12_descriptor_allocator.h>
#include <engine/render_backends/dx12/dx12_submit.h>
#include <gpu/dx12/dx12_internal.h>
#include <gpu/gaussian_splat_color_lod_settings.h>
#include <gpu/gaussian_splat_coverage_settings.h>


namespace wz::gpu::dx12
{
    struct ScalarFieldDebugContext
    {
        ID3D12RootSignature* root_sig = nullptr;
        ID3D12PipelineState* pso = nullptr;
        GPUHandle scalar_field_texture{};

        float display_min = 0.0f;
        float display_max = 1.0f;
        bool normalize_for_display = true;
    };

    struct MeshWireframeDebugContext
    {
        ID3D12RootSignature* root_sig = nullptr;
        ID3D12PipelineState* pso = nullptr;

        GPUHandle mesh{};
    };

    struct GaussianSplatDebugContext
    {
        ID3D12RootSignature* root_sig = nullptr;
        ID3D12PipelineState* pso = nullptr;

        GPUHandle splat_cloud{};
    };

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

        // depth target (single shared D32_FLOAT for all programs that opt in
        // via DepthMode::TestNoWrite / DepthMode::TestWrite).  Created at
        // device init, recreated on resize, released at shutdown.
        ID3D12DescriptorHeap* dsv_heap = nullptr;
        ID3D12Resource*       depth_buffer = nullptr;

        UINT frame_index = 0;

        HWND hwnd = nullptr;
        UINT width = 0;
        UINT height = 0;

        //scalar field
        ID3D12DescriptorHeap* scalar_field_srv_heap = nullptr;
        UINT scalar_field_srv_stride = 0;
        uint32_t scalar_field_srv_capacity = 1024;
        uint32_t scalar_field_srv_count = 0;

        ScalarFieldDebugContext* scalar_debug_ctx = nullptr;
        MeshWireframeDebugContext* mesh_wire_debug_ctx = nullptr;
        GaussianSplatDebugContext* gaussian_splat_debug_ctx = nullptr;

        wz::gpu::dx12::DX12ShaderTable shaders;
        wz::gpu::dx12::internal::DX12ScalarFieldTextureTable scalar_field_textures;
        wz::gpu::dx12::internal::DX12MeshTable meshes;
        wz::gpu::dx12::internal::DX12GaussianSplatCloudTable gaussian_splat_clouds;
        wz::gpu::dx12::internal::DX12GraphicsPipelineTable graphics_pipelines;

        // General-purpose shader-visible CBV/SRV/UAV heap.
        // Used for SRV descriptor tables (e.g., SplatPull StructuredBuffer).
        wz::gpu::dx12::DX12DescriptorAllocator srv_cbv_uav_allocator;

        wz::render::backend::dx12::Context* ctx = nullptr;

        // Scene-wide splat color LOD settings.  Pushed per-frame by the
        // toolhost via wz::gpu::set_splat_color_lod_settings(); consumed
        // by the dx12 submit path when binding NeighborhoodColorBlend.
        wz::gpu::SplatColorLODSettings splat_color_lod_settings{};

        // Scene-wide splat coverage settings.  Pushed per-frame by the
        // toolhost via wz::gpu::set_splat_coverage_settings(); consumed
        // by the dx12 submit path when binding GaussianSplatTerrainCoverageDebug.
        wz::gpu::SplatCoverageSettings splat_coverage_settings{};
    };

}
