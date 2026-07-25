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
#include <gpu/dx12/dx12_internal.h>
#include <gpu/gaussian_splat_color_lod_settings.h>
#include <gpu/gaussian_splat_coverage_settings.h>

// The engine-layer submit context is only stored by pointer; the full type
// lives in engine/render_backends/dx12/dx12_submit.h, included by the TUs
// that actually call into the submit path.
namespace wz::engine::render_backend::dx12 { struct Context; }


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

    // Offscreen render-to-texture display (S6): a fullscreen-triangle blit that
    // samples an arbitrary RGBA texture onto the current render target. Lazily
    // built on the first blit and reused; the SRV heap holds one shader-visible
    // descriptor rewritten per blit for the source texture.
    struct BlitContext
    {
        ID3D12RootSignature*  root_sig  = nullptr;
        ID3D12PipelineState*  pso       = nullptr;
        ID3D12DescriptorHeap* srv_heap  = nullptr;
    };

    // Textured 3D mesh (S6 3D-mesh consumer): draw a textured quad transformed by a
    // caller MVP, sampling an arbitrary texture -- the RTT texture on a world
    // surface. Same lazy root-sig/PSO/SRV-heap shape as BlitContext, plus a 16-float
    // MVP root constant.
    struct TexturedQuadContext
    {
        ID3D12RootSignature*  root_sig  = nullptr;
        ID3D12PipelineState*  pso       = nullptr;
        ID3D12DescriptorHeap* srv_heap  = nullptr;
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
        BlitContext* blit_ctx = nullptr;
        TexturedQuadContext* textured_quad_ctx = nullptr;
        MeshWireframeDebugContext* mesh_wire_debug_ctx = nullptr;
        GaussianSplatDebugContext* gaussian_splat_debug_ctx = nullptr;

        wz::gpu::dx12::DX12ShaderTable shaders;
        wz::gpu::dx12::internal::DX12TextureTable textures;
        wz::gpu::dx12::internal::DX12ScalarFieldTextureTable scalar_field_textures;
        wz::gpu::dx12::internal::DX12MeshTable meshes;
        wz::gpu::dx12::internal::DX12MeshFieldVisualizationTable mesh_field_visualizations;
        wz::gpu::dx12::internal::DX12GaussianSplatCloudTable gaussian_splat_clouds;
        wz::gpu::dx12::internal::DX12GraphicsPipelineTable graphics_pipelines;
        wz::gpu::dx12::internal::DX12ComputeBufferTable compute_buffers;
        wz::gpu::dx12::internal::DX12ComputePipelineTable compute_pipelines;

        // General-purpose shader-visible CBV/SRV/UAV heap.
        // Used for SRV descriptor tables (e.g., SplatPull StructuredBuffer).
        wz::gpu::dx12::DX12DescriptorAllocator srv_cbv_uav_allocator;

        wz::engine::render_backend::dx12::Context* ctx = nullptr;

        // Scene-wide splat color LOD settings.  Pushed per-frame by the
        // toolhost via wz::gpu::set_splat_color_lod_settings(); consumed
        // by the dx12 submit path when binding NeighborhoodColorBlend.
        wz::gpu::SplatColorLODSettings splat_color_lod_settings{};

        // Scene-wide splat coverage settings.  Pushed per-frame by the
        // toolhost via wz::gpu::set_splat_coverage_settings(); consumed
        // by the dx12 submit path when binding GaussianSplatTerrainCoverageDebug.
        wz::gpu::SplatCoverageSettings splat_coverage_settings{};

        wz::gpu::DeviceStatus status = wz::gpu::DeviceStatus::Ok;
        wz::gpu::DeviceLostInfo lost_info{};
    };

    bool dx12_is_device_lost_hr(HRESULT hr) noexcept;
    bool dx12_device_lost(const DX12Device& device) noexcept;
    bool dx12_check_hr(DX12Device& device, HRESULT hr, const char* operation);
    void dx12_mark_device_lost(
        DX12Device& device,
        HRESULT hr,
        const char* operation);
    wz::gpu::DeviceStatus dx12_device_status(
        const DX12Device* device) noexcept;
    const wz::gpu::DeviceLostInfo* dx12_device_lost_info(
        const DX12Device* device) noexcept;

}
