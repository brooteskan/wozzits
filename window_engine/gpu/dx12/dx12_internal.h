#pragma once

// file: gpu/dx12/dx12_internal.h

#include <span>
#include <vector>
#include <gpu/gpu.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wswitch"
#include <gpu/dx12/external/d3dx12.h>
#pragma clang diagnostic pop
#include <gpu/dx12/dx12_descriptor_allocator.h>
#include <gpu/gpu_types.h>
#include <gpu/shader.h>
#include <gpu/gaussian_splat_color_lod_settings.h>
#include <gpu/gaussian_splat_coverage_settings.h>
#include <d3dcompiler.h>
#include <d3d12.h>
#include <engine/assets/render_program/render_program.h>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;

namespace wz::gpu::dx12
{
    struct DX12Shader;
}

namespace wz::gpu::dx12::internal
{
    ID3D12Device* get_device(Device& d);
    ID3D12GraphicsCommandList* get_command_list(Device& d);
    ID3D12RootSignature* create_empty_root_signature(ID3D12Device* device);

    // added for imgui tooling integration.
    DXGI_FORMAT get_backbuffer_format();
    UINT get_backbuffer_count();
    ID3D12CommandQueue* get_command_queue(Device& d);
    D3D12_CPU_DESCRIPTOR_HANDLE get_current_rtv(Device& d);
    D3D12_CPU_DESCRIPTOR_HANDLE get_dsv(Device& d);
    UINT get_width(Device& d);
    UINT get_height(Device& d);

    ID3D12PipelineState* create_triangle_pso(
        wz::gpu::Device& device,
        ID3D12RootSignature* root_sig,
        wz::gpu::GPUHandle vertex_shader,
        wz::gpu::GPUHandle pixel_shader
    );

    wz::gpu::GPUHandle store_shader(
        wz::gpu::Device& device,
        ID3DBlob* blob,
        wz::gpu::ShaderStage stage
    );

    const wz::gpu::dx12::DX12Shader* get_shader(
        wz::gpu::Device& device,
        wz::gpu::GPUHandle handle
    );
    
}

// ── Scalar field texture ─────────────────────────────────────────
namespace wz::engine::assets {
    struct ScalarFieldData;
}

namespace wz::gpu::dx12::internal {

    struct DX12ScalarFieldTexture
    {
        ID3D12Resource* texture = nullptr;

        D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu{};

        uint32_t width = 0;
        uint32_t height = 0;
    };

    class DX12ScalarFieldTextureTable
    {
    public:
        DX12ScalarFieldTextureTable();

        GPUHandle add(DX12ScalarFieldTexture tex);
        const DX12ScalarFieldTexture* get(GPUHandle handle) const;
        void destroy();

    private:
        struct Slot
        {
            uint32_t epoch = 0;
            bool occupied = false;
            DX12ScalarFieldTexture tex{};
        };

        std::vector<Slot> slots_;
    };

    GPUHandle upload_scalar_field_texture_dx12(
        Device& device,
        const wz::engine::assets::ScalarFieldData& field
    );
}



// ── Mesh buffers ──────────────────────────────────────────────────

namespace wz::engine::assets {
    struct MeshData;
}

namespace wz::gpu::dx12::internal {

    struct DX12MeshResource
    {
        ID3D12Resource* vertex_buffer = nullptr;
        ID3D12Resource* index_buffer = nullptr;

        // Kept alive for V1 until we have explicit upload/fence lifetime handling.
        ID3D12Resource* vertex_upload = nullptr;
        ID3D12Resource* index_upload = nullptr;

        D3D12_VERTEX_BUFFER_VIEW vertex_view{};
        D3D12_INDEX_BUFFER_VIEW  index_view{};

        uint32_t vertex_count = 0;
        uint32_t index_count = 0;
    };

    class DX12MeshTable
    {
    public:
        DX12MeshTable();

        GPUHandle add(DX12MeshResource mesh);
        const DX12MeshResource* get(GPUHandle handle) const;
        void destroy();

    private:
        struct Slot
        {
            uint32_t epoch = 0;
            bool occupied = false;
            DX12MeshResource mesh{};
        };

        std::vector<Slot> slots_;
    };

    GPUHandle upload_mesh_dx12(
        Device& device,
        const wz::engine::assets::MeshData& mesh
    );

    const DX12MeshResource* get_mesh(
        Device& device,
        GPUHandle handle
    );
}


// ── Gaussian splat buffers ────────────────────────────────────────

namespace wz::engine::assets {
    struct GaussianSplatCloudData;
    struct GaussianSplatColorLODData;
}

namespace wz::gpu::dx12::internal {

    struct DX12GaussianSplatVertex
    {
        // POSITION.xyz + OPACITY
        float position[3] = {};
        float opacity = 1.0f;

        // SCALE.xyz + padding
        // Decoded from log-space PLY scale into world-space scale.
        float scale[3] = { 1.0f, 1.0f, 1.0f };
        float pad0 = 0.0f;

        // ROTATION.xyzw
        // PLY convention: rot_0=w, rot_1=x, rot_2=y, rot_3=z.
        // Stored here as x,y,z,w for shader convenience.
        float rotation[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

        // COLOR.rgb (base, decoded from SH DC)
        float color[3] = { 1.0f, 1.0f, 1.0f };

        // LOD color + confidence, packed RGBA8 (R,G,B = neighborhood color,
        // A = confidence in [0,1]).  When no LOD asset is present, this is
        // packed from `color` with confidence = 0 so the shader sees a
        // safe fallback.  Replaces the old `pad1` slot — same offset/size,
        // preserving the 64-byte structured-buffer stride.
        uint32_t lod_color_confidence_rgba8 = 0;
    };

    // Layout guards: the HLSL StructuredBuffer<DX12GaussianSplatVertex> mirror
    // must match these offsets exactly.  Update the pull shader if any change.
    static_assert(sizeof(DX12GaussianSplatVertex)             == 64);
    static_assert(offsetof(DX12GaussianSplatVertex, position) ==  0);
    static_assert(offsetof(DX12GaussianSplatVertex, opacity)  == 12);
    static_assert(offsetof(DX12GaussianSplatVertex, scale)    == 16);
    static_assert(offsetof(DX12GaussianSplatVertex, pad0)     == 28);
    static_assert(offsetof(DX12GaussianSplatVertex, rotation) == 32);
    static_assert(offsetof(DX12GaussianSplatVertex, color)    == 48);
    static_assert(offsetof(DX12GaussianSplatVertex, lod_color_confidence_rgba8) == 60);

    struct DX12GaussianSplatCloudResource
    {
        ID3D12Resource* vertex_buffer       = nullptr;  // StructuredBuffer<Splat> (t0)
        ID3D12Resource* sorted_index_buffer = nullptr;  // StructuredBuffer<uint>  (t1)

        // Persistently mapped write pointer for sorted_index_buffer.
        // The sort follow-on writes new index orders here before DrawInstanced.
        // Null until sorted_index_buffer is successfully created.
        uint32_t* sorted_index_map = nullptr;

        D3D12_VERTEX_BUFFER_VIEW vertex_view{};

        uint32_t splat_count = 0;

        // Descriptor table covering both SRV slots:
        //   slot 0 → StructuredBuffer<Splat>  (t0, SplatCloud)
        //   slot 1 → StructuredBuffer<uint>   (t1, SortedSplatIndices)
        // Valid only when the device's srv_cbv_uav_allocator had capacity at
        // upload time and both buffers were created successfully.
        wz::gpu::dx12::DX12DescriptorTable srv_table{};

        // Binding-model-specific validity checks.
        // Use these instead of a generic `valid()` so submit paths cannot
        // accidentally treat a partially-initialized resource as fully ready.
        bool valid_for_vertex_instanced() const noexcept
        {
            return vertex_buffer != nullptr && splat_count > 0;
        }

        bool valid_for_splat_pull() const noexcept
        {
            return vertex_buffer       != nullptr
                && sorted_index_buffer != nullptr
                && splat_count          > 0
                && srv_table.valid();
        }
    };

    class DX12GaussianSplatCloudTable
    {
    public:
        DX12GaussianSplatCloudTable();

        GPUHandle add(DX12GaussianSplatCloudResource cloud);
        const DX12GaussianSplatCloudResource* get(GPUHandle handle) const;
        void destroy();

    private:
        struct Slot
        {
            uint32_t epoch = 0;
            bool occupied = false;
            DX12GaussianSplatCloudResource cloud{};
        };

        std::vector<Slot> slots_;
    };

    GPUHandle upload_gaussian_splat_cloud_dx12(
        Device& device,
        const wz::engine::assets::GaussianSplatCloudData& cloud,
        const wz::engine::assets::GaussianSplatColorLODData* lod = nullptr);

    const DX12GaussianSplatCloudResource* get_gaussian_splat_cloud(
        Device& device,
        GPUHandle handle);

    // Scene-wide splat color LOD settings, last pushed by
    // wz::gpu::set_splat_color_lod_settings().  Default is Natural mode.
    const wz::gpu::SplatColorLODSettings& get_lod_settings(Device& device);

    // Scene-wide splat coverage settings, last pushed by
    // wz::gpu::set_splat_coverage_settings().  Default is TransparentBlend.
    const wz::gpu::SplatCoverageSettings& get_coverage_settings(Device& device);

    // Write externally-computed sorted indices into a cloud's persistently-mapped
    // t1 SortedIndices upload buffer.
    //
    // sorted_indices must have the same size as cloud.splat_count; mismatches are
    // rejected with a log message and the existing identity buffer is preserved.
    // No-op if sorted_indices is empty (caller signals "use identity t1 buffer").
    //
    // Thread safety: caller is responsible for ensuring the GPU is not reading
    // sorted_index_buffer when this is called (i.e. call before DrawInstanced).
    void update_sorted_indices(
        const DX12GaussianSplatCloudResource& cloud,
        std::span<const uint32_t>             sorted_indices);
}


// ── Gaussian splat debug pipeline ref ────────────────────────────
//
// Non-owning view of the PSO and root signature created by
// create_gaussian_splat_debug_context().  The resolver-based submit
// path uses this to bind the splat pipeline without reaching into
// device internals.

namespace wz::gpu::dx12::internal {

    struct GaussianSplatDebugPipelineRef
    {
        ID3D12RootSignature* root_sig = nullptr;
        ID3D12PipelineState* pso      = nullptr;

        bool valid() const noexcept { return root_sig && pso; }
    };

    GaussianSplatDebugPipelineRef get_gaussian_splat_debug_pipeline(Device& d);
}


// ── Mesh wireframe debug pipeline ref ────────────────────────────
//
// Non-owning view of the PSO and root signature created by
// create_mesh_wireframe_debug_context().  The resolver-based submit
// path uses this to draw Mesh DrawCommands without reaching into
// device internals.

namespace wz::gpu::dx12::internal {

    struct MeshWireframePipelineRef
    {
        ID3D12RootSignature* root_sig = nullptr;
        ID3D12PipelineState* pso      = nullptr;

        bool valid() const noexcept { return root_sig && pso; }
    };

    MeshWireframePipelineRef get_mesh_wireframe_pipeline(Device& d);
}


// ── Graphics pipeline table ───────────────────────────────────────
//
// Stores PSO + root signature pairs created by RenderablePipelineCache.
// Keyed by GPUHandle of type kAssetTypeGPUGraphicsPipeline (532).
// The debug context singletons are separate; this table is for the
// production pipeline cache path.

#include <engine/assets/renderable/renderable.h>
#include <engine/assets/type_extensions.h>

namespace wz::gpu::dx12::internal {

    struct DX12GraphicsPipeline
    {
        ID3D12RootSignature* root_sig = nullptr;
        ID3D12PipelineState* pso      = nullptr;

        bool valid() const noexcept { return root_sig && pso; }
    };

    class DX12GraphicsPipelineTable
    {
    public:
        DX12GraphicsPipelineTable();

        GPUHandle add(DX12GraphicsPipeline pipeline);
        const DX12GraphicsPipeline* get(GPUHandle handle) const;
        void destroy();

    private:
        struct Slot
        {
            uint32_t epoch    = 0;
            bool     occupied = false;
            DX12GraphicsPipeline pipeline{};
        };

        std::vector<Slot> slots_;
    };

    // Create a graphics pipeline (root sig + PSO) for the given builtin program,
    // store it in the device's pipeline table, and return its GPUHandle.
    GPUHandle create_graphics_pipeline(
        Device& device,
        wz::engine::assets::BuiltinRenderProgram program,
        GPUHandle vertex_shader,
        GPUHandle pixel_shader);

    // Data-driven variant: build root sig + PSO from RenderProgramData.
    GPUHandle create_graphics_pipeline_from_data(
        Device& device,
        const wz::engine::assets::RenderProgramData& data,
        GPUHandle vertex_shader,
        GPUHandle pixel_shader);

    // Retrieve a previously created graphics pipeline.
    // Returns nullptr if the handle is invalid or the slot is empty.
    const DX12GraphicsPipeline* get_graphics_pipeline(
        Device& device,
        GPUHandle handle);

    // Returns the shader-visible CBV/SRV/UAV descriptor heap used by the
    // device's static allocator.  Must be bound via SetDescriptorHeaps before
    // any SetGraphicsRootDescriptorTable call that references it.
    // Returns nullptr if the device is not initialized.
    ID3D12DescriptorHeap* get_srv_cbv_uav_heap(Device& device);
}