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
#include <gpu/gaussian_splat_coverage_settings.h>

#include <vector>

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
    // built on the first blit and reused. The SRV heap is a per-frame RING, one
    // slot consumed per blit: descriptors are read by the GPU at execute time,
    // long after they are written, so a slot must never be rewritten within a
    // frame — every recorded blit would sample the LAST texture written.
    // srv_cursor resets in begin_frame.
    struct BlitContext
    {
        static constexpr uint32_t kSrvCapacity = 16;

        ID3D12RootSignature*  root_sig        = nullptr;
        ID3D12PipelineState*  pso             = nullptr;
        // Second PSO sharing root_sig + srv_heap: same fullscreen sample, but the
        // pixel shader applies the linear->sRGB transfer. Used by the #324 encode
        // pass that resolves the RGBA16F scene target to the sRGB backbuffer.
        ID3D12PipelineState*  pso_srgb_encode = nullptr;
        ID3D12DescriptorHeap* srv_heap        = nullptr;
        UINT                  srv_stride      = 0;
        uint32_t              srv_cursor      = 0;
    };

    // Textured 3D mesh (S6 3D-mesh consumer): draw a textured quad transformed by a
    // caller MVP, sampling an arbitrary texture -- the RTT texture on a world
    // surface. Same lazy root-sig/SRV-heap shape as BlitContext, plus a 16-float MVP
    // root constant. Two PSOs share the root sig: `pso` is the screen-space overlay
    // (opaque, depth disabled) used by the 2D-surface consumer; `pso_world` is the
    // in-scene surface (premultiplied-alpha over the scene, depth-tested no-write
    // against the shared D32 depth) so a world-anchored card composites and is
    // occluded by scene geometry.
    struct TexturedQuadContext
    {
        // Per-frame SRV ring, one slot per quad draw (see BlitContext): a slot
        // rewritten within a frame would retroactively change every earlier
        // recorded quad's source texture. Sized for the worst realistic frame
        // (composite layers + overlays + cards). srv_cursor resets in
        // begin_frame; an overflowing draw fails loudly instead of aliasing.
        static constexpr uint32_t kSrvCapacity = 256;

        ID3D12RootSignature*  root_sig      = nullptr;
        ID3D12PipelineState*  pso           = nullptr;  // overlay: opaque, no depth
        ID3D12PipelineState*  pso_world     = nullptr;  // in-scene: alpha + depth
        ID3D12PipelineState*  pso_composite = nullptr;  // into a texture: alpha only
        ID3D12DescriptorHeap* srv_heap      = nullptr;
        UINT                  srv_stride    = 0;
        uint32_t              srv_cursor    = 0;
    };

    struct DX12Device
    {
        // Number of frames the CPU may have in flight ahead of the GPU (#306).
        // Declared first so the per-slot arrays below can size themselves by it.
        static constexpr UINT kFramesInFlight = 2;

        //fences
        ID3D12Fence* fence = nullptr;
        HANDLE fence_event = nullptr;
        // Starts at 1: 0 is the "slot never used" sentinel for slot_fence_value
        // below, so a real submission's fence value must never be 0.
        UINT64 fence_value = 1;

        // Staging buffers referenced by an in-flight frame's recorded in-frame
        // copies (record_compute_buffer_update_dx12) -- one list per frame slot
        // (#306). A slot's staging is released in begin_frame only after that
        // slot's prior frame has passed its fence, so the GPU is done reading it.
        std::vector<ID3D12Resource*> frame_upload_staging[kFramesInFlight];

        // One-shot copy/upload fence, separate from the frame fence above. The
        // frame fence's values ARE the rhi registry's reclamation timeline:
        // frame_timeline_value() promises the value it returns is signaled
        // only by end_frame, AFTER the frame's recorded draws execute. A
        // mid-frame synchronous upload signaling that same value would make
        // completed_timeline_value() reach it while the frame's command list
        // is still unsubmitted — any release + collect in that window then
        // destroys resources the recorded frame still references (#311).
        ID3D12Fence* copy_fence = nullptr;
        HANDLE copy_fence_event = nullptr;
        UINT64 copy_fence_value = 0;

        // core
        ID3D12Device* device = nullptr;
        IDXGISwapChain3* swapchain = nullptr;
        ID3D12CommandQueue* queue = nullptr;

        // The debug layer's message sink, when the layer is installed (debug
        // builds only; null otherwise). Drained by publish_debug_diagnostics(),
        // which publishes each message as a DiagnosticRecord to the LoggerService
        // lane (#291) -- before #317 the layer was enabled and nobody held this
        // interface, so its verdicts were invisible outside a debugger.
        ID3D12InfoQueue* info_queue = nullptr;

        // Whether install_debug_storage_filter has a severity filter on the stack
        // to pop before pushing a replacement. Tracked rather than assumed, because
        // popping a stack we never pushed to is not harmless.
        //
        // The per-id deny-after-8 tally that used to live here (#317) is GONE
        // (#291): it stopped D3D12 storing a repeating id to keep the log quiet,
        // which lost the true count -- "1584 ClearRenderTargetView #820 in 27s"
        // became "suppressed after 8". State-side dedup on the LoggerService lane
        // keeps the exact total now, so the source no longer suppresses per id; the
        // severity storage filter (no INFO/MESSAGE) stays as the volume cap.
        bool debug_filter_pushed = false;
        // Reused by GetMessage so a drain does not allocate per message.
        std::vector<char> debug_scratch;

        // frame -- N-in-flight ring (#306). One command allocator per in-flight
        // frame slot, so a slot's allocator is reset only after that slot's prior
        // frame has passed its fence; `cmd` is re-targeted onto the current slot's
        // allocator each begin_frame. frame_slot advances every begin_frame;
        // slot_fence_value[i] is the fence value the last frame using slot i
        // signalled (0 == never used) -- begin_frame waits on it before reusing
        // the slot (the pacing wait that replaced end_frame's blocking wait).
        ID3D12CommandAllocator* allocators[kFramesInFlight] = {};
        ID3D12GraphicsCommandList* cmd = nullptr;
        UINT   frame_slot = 0;
        UINT64 slot_fence_value[kFramesInFlight] = {};
        // Runtime ring depth, <= kFramesInFlight. 2 == pipelined (default);
        // WZ_FRAMES_IN_FLIGHT=1 forces serial (slot 0 only, begin_frame waits on
        // the immediately-prior frame) for an A/B of the overlap (#306). Set once
        // at device creation.
        UINT   frames_in_flight = kFramesInFlight;

        // render target
        ID3D12DescriptorHeap* rtv_heap = nullptr;
        ID3D12Resource* backbuffers[2] = {};
        UINT rtv_stride = 0;

        // depth target (single shared D32_FLOAT for all programs that opt in
        // via DepthMode::TestNoWrite / DepthMode::TestWrite).  Created at
        // device init, recreated on resize, released at shutdown.
        ID3D12DescriptorHeap* dsv_heap = nullptr;
        ID3D12Resource*       depth_buffer = nullptr;

        // Whether the CURRENTLY BOUND pass has a depth-stencil view.
        //
        // D3D12 requires a pipeline's DSVFormat to be UNKNOWN when the bound
        // DSV is null; a pipeline declaring D32_FLOAT against no DSV is an
        // EXECUTION ERROR (#615) and its draw is undefined. The PSO builder
        // reads this so the pipeline can honour what the pass actually bound
        // -- which is the model begin_frame's own comment describes ("the
        // bound DSV is then ignored" for a depth-disabled program). See #317.
        //
        // INVARIANT: every OMSetRenderTargets in this layer must set it.
        // `grep -n OMSetRenderTargets src/gpu/dx12/*.cpp` is the whole list.
        bool depth_target_bound = false;

        // The DXGI format of the CURRENTLY BOUND colour target. The PSO builder
        // keys on it exactly like depth_target_bound above: a program drawn into
        // the RGBA16F scene target and into the RGBA8 backbuffer needs two PSOs,
        // because a PSO's RTVFormats[0] must match the bound RTV's format. Read
        // from the device, not plumbed, so it cannot disagree with the last
        // OMSetRenderTargets. INVARIANT: every OMSetRenderTargets in this layer
        // must set it (same grep as depth_target_bound). #324 (H18 on #316).
        DXGI_FORMAT bound_color_format = DXGI_FORMAT_R8G8B8A8_UNORM;

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

        wz::gpu::dx12::DX12ShaderTable shaders;
        wz::gpu::dx12::internal::DX12TextureTable textures;
        wz::gpu::dx12::internal::DX12ScalarFieldTextureTable scalar_field_textures;
        wz::gpu::dx12::internal::DX12MeshTable meshes;
        wz::gpu::dx12::internal::DX12MeshFieldVisualizationTable mesh_field_visualizations;
        wz::gpu::dx12::internal::DX12GraphicsPipelineTable graphics_pipelines;
        wz::gpu::dx12::internal::DX12ComputeBufferTable compute_buffers;
        wz::gpu::dx12::internal::DX12ComputePipelineTable compute_pipelines;

        // General-purpose shader-visible CBV/SRV/UAV heap.
        // Used for SRV descriptor tables (e.g., SplatPull StructuredBuffer).
        wz::gpu::dx12::DX12DescriptorAllocator srv_cbv_uav_allocator;


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

    // Wait for everything submitted so far on the queue, on the COPY fence.
    // The single wait every one-shot upload/copy submission must use — never
    // the frame fence (see DX12Device::copy_fence).
    bool copy_queue_wait(DX12Device* impl);

}
