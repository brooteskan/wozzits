#pragma once

// file: gpu/dx12/dx12_internal.h

#include <span>
#include <string>
#include <vector>
#include <gpu/gpu.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wswitch"
#include <gpu/dx12/external/d3dx12.h>
#pragma clang diagnostic pop
#include <gpu/dx12/dx12_descriptor_allocator.h>
#include <gpu/gpu_types.h>
#include <gpu/shader.h>
#include <gpu/gaussian_splat_coverage_settings.h>
#include <diagnostics/diagnostic_record.h>  // DiagnosticSeverity (debug-layer producer)
#include <d3dcompiler.h>
#include <d3d12.h>
#include <engine/assets/render_program/render_program.h>
#include <engine/assets/compute_pipeline/compute_pipeline.h>

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

    // Does the CURRENTLY BOUND pass have a depth-stencil view?
    //
    // D3D12 requires a pipeline's DSVFormat to be UNKNOWN when the bound DSV is
    // null. The offscreen render-to-texture pass deliberately binds no depth,
    // while the PSO builder derived DSVFormat from the program's depth_mode
    // alone -- so every authored-RTT pass drew depth-enabled pipelines against
    // a null DSV: EXECUTION ERROR #615, undefined draws, and green test suites
    // because nobody read the debug layer. The PSO cache keys on this so the
    // two variants of a program can coexist. See #317.
    [[nodiscard]] bool depth_target_bound(Device& d);

    // Record what the OMSetRenderTargets you just issued actually bound.
    // Every render-target bind in this layer must call it -- `grep -n
    // OMSetRenderTargets src/gpu/dx12/*.cpp` is the whole list.
    void set_depth_target_bound(Device& d, bool bound);

    // The DXGI format of the currently-bound colour target. Twin of
    // depth_target_bound: the PSO cache keys on it so a program drawn into the
    // RGBA16F scene target and into the RGBA8 backbuffer gets one pipeline each.
    // Same bind-site invariant as set_depth_target_bound. #324 (H18 on #316).
    [[nodiscard]] DXGI_FORMAT bound_color_format(Device& d);
    void set_bound_color_format(Device& d, DXGI_FORMAT format);

    // THE mapping from wz::rhi::BlendMode to DX12 render-target blend state.
    //
    // There used to be two, and they disagreed. #272 unified the ENUM (the
    // engine consumes wz::rhi::BlendMode directly) but never unified the
    // TRANSLATION, so the legacy factory in dx12_pipeline_factory.cpp kept its
    // own if/else chain -- and was then left behind by BOTH commits that added
    // members: e74167af (Additive, #266) and f45720b2 (SourceAtop +
    // SliceFromDestination, #299) each touched only render_program_compilers.cpp
    // and rhi_dx12_pipeline.cpp. Those three modes rendered fully OPAQUE
    // wherever the legacy factory realized the PSO, while rendering correctly
    // through the rhi path (#317).
    //
    // Both PSO factories now call this. It is a switch with NO default and a
    // static_assert on the enum's last member beside its definition, so adding
    // a BlendMode in the wozzits-rhi repo fails the ENGINE build instead of
    // silently falling through to D3D12_DEFAULT (= BlendEnable FALSE = opaque).
    // The previous if/else chains could not produce a diagnostic at all: an
    // if/else chain has no exhaustiveness check, and the build is /W3 with no
    // /WX, so even a switch would only have warned.
    //
    // Returns the RenderTarget[0] state; callers keep starting from
    // CD3DX12_BLEND_DESC(D3D12_DEFAULT) for the remaining seven slots.
    [[nodiscard]] D3D12_RENDER_TARGET_BLEND_DESC
    render_target_blend_desc(wz::rhi::BlendMode mode) noexcept;

    // Pass attribution stamped into each published DiagnosticRecord (#291): the
    // two units a frame records -- the main pass, and an authored render target's
    // offscreen pass. The pass is the unit that is wrong, captured at the source so
    // the cold LoggerService reporter never has to reconstruct it. The reporter
    // maps these back to "main"/"offscreen" labels.
    inline constexpr uint16_t kDebugPassMain      = 0;
    inline constexpr uint16_t kDebugPassOffscreen = 1;

    // Map a D3D12 debug-layer severity onto the engine's DiagnosticSeverity. INFO
    // and MESSAGE (per-resource chatter, denied at storage) fold into Info. Free
    // function in the header so it is testable with no device -- the same reason
    // dx12_input_element_semantic lives out here (#317, D1-C7).
    [[nodiscard]] inline wz::diag::DiagnosticSeverity to_diagnostic_severity(
        D3D12_MESSAGE_SEVERITY severity) noexcept
    {
        switch (severity) {
            case D3D12_MESSAGE_SEVERITY_CORRUPTION:
                return wz::diag::DiagnosticSeverity::Corruption;
            case D3D12_MESSAGE_SEVERITY_ERROR:
                return wz::diag::DiagnosticSeverity::Error;
            case D3D12_MESSAGE_SEVERITY_WARNING:
                return wz::diag::DiagnosticSeverity::Warning;
            default:  // INFO / MESSAGE
                return wz::diag::DiagnosticSeverity::Info;
        }
    }

    // Drain the D3D12 debug layer for the pass just recorded and PUBLISH each
    // distinct stored message (CORRUPTION/ERROR/WARNING) as a DiagnosticRecord --
    // id/severity/category + a per-drain occurrence count, stamped with `pass` --
    // to the installed diagnostic sink, then clear the queue. NO string is built
    // and NO logger is called on this (render / GPUOwner) lane: detection is cheap
    // ints, and the cold LoggerService lane keeps the exact count and reports on
    // its cadence (#291). A no-op when the debug layer is absent (release) or no
    // sink is installed.
    //
    // Replaces take_debug_messages + its per-id deny-after-8 tally: state-side
    // dedup on the lane keeps the true total the tally had to throw away (it stopped
    // D3D12 storing an id to dedup it). The severity storage filter stays as the
    // volume cap (INFO/MESSAGE never stored); the per-id deny list is gone, so a
    // repeating warning now re-stores each frame and the drain runs each frame --
    // cheap (ints only), which is the trade #291 makes for the exact count.
    //
    // Backstory: the debug layer has always been enabled in debug builds with
    // nothing listening -- every verdict went only to the native debug stream, so a
    // hard EXECUTION ERROR (#615, depth-enabled PSO vs a null DSV) shipped green in
    // two suites (#317). Now it feeds the diagnostics channel.
    void publish_debug_diagnostics(Device& d, uint16_t pass);

    ID3D12RootSignature* create_empty_root_signature(ID3D12Device* device);

    DXGI_FORMAT get_backbuffer_format();
    D3D12_CPU_DESCRIPTOR_HANDLE get_current_rtv(Device& d);
    D3D12_CPU_DESCRIPTOR_HANDLE get_dsv(Device& d);
    UINT get_width(Device& d);
    UINT get_height(Device& d);

    // Offscreen render-to-texture (S6). See the definitions in dx12_device.cpp
    // (begin/end pass) and dx12_texture.cpp (transitions + readback).
    bool begin_offscreen_pass(Device& d, GPUHandle rt, const float clear_color[4]);
    bool end_offscreen_pass(Device& d, GPUHandle rt);
    // Like begin_offscreen_pass, but binds the shared depth buffer alongside the
    // colour target (the offscreen path is depth-less, for the mask/overlay use).
    // The scene's main pass uses this to render depth-tested into a full-screen
    // RGBA16F target; end_ transitions it to shader-read and rebinds the
    // backbuffer so the encode + overlays + present land on screen. #324.
    bool begin_primary_color_pass(Device& d, GPUHandle color_target,
                                  const float clear_color[4]);
    bool end_primary_color_pass(Device& d, GPUHandle color_target);
    bool transition_texture_to_render_target_dx12(Device& device, GPUHandle handle);
    bool transition_texture_to_shader_read_dx12(Device& device, GPUHandle handle);
    bool read_texture_rgba8_dx12(
        Device& device, GPUHandle handle, std::vector<uint8_t>& out);
    bool read_backbuffer_rgba8_dx12(Device& device, std::vector<uint8_t>& out);
    // Draw a texture onto the currently-bound render target as a fullscreen quad
    // (S6 "2D surface" consumer). See dx12_blit.cpp.
    bool blit_texture_dx12(Device& device, GPUHandle texture);
    // Fullscreen pass that samples a LINEAR texture, applies the linear->sRGB
    // transfer, and writes the currently-bound (sRGB-less UNORM) backbuffer --
    // the #324 encode that turns the RGBA16F scene target into display-referred
    // pixels. The ONE place gamma is applied; anything drawn after is untouched.
    bool encode_srgb_to_backbuffer_dx12(Device& device, GPUHandle linear_texture);
    // How a textured quad draws. See dx12_textured_quad.cpp.
    enum class TexturedQuadMode : std::uint8_t
    {
        Overlay,       // opaque, depth off -- a screen-space / 2D surface
        WorldSurface,  // premultiplied alpha + depth test (no write) -- in-scene
        Composite,     // premultiplied alpha, depth off -- drawing INTO a texture
    };

    // Draw a texture on a unit quad transformed by a column-major MVP, multiplied
    // by an optional RGBA tint (nullptr = opaque white). The S6 "3D-mesh surface"
    // consumer; also the primitive the compositor below is built from.
    bool draw_textured_quad_dx12(
        Device& device, GPUHandle texture, const float mvp[16],
        TexturedQuadMode mode = TexturedQuadMode::Overlay,
        const float tint_rgba[4] = nullptr);

    // One layer of a texture composite: a source texture placed into the target's
    // UV space by centre + half-extent (both in [0,1] target UV), rotated about
    // that centre, scaled by `opacity`.
    struct TextureCompositeLayer
    {
        GPUHandle texture{};
        float     center_uv[2]    = { 0.5f, 0.5f };
        float     half_size_uv[2] = { 0.5f, 0.5f };
        float     rotation        = 0.0f;   // radians
        float     opacity         = 1.0f;
    };

    // Composite `layers` into `target` (a render-target texture): clear to
    // base_color, then draw each layer over it with premultiplied-alpha blending,
    // in order. The general "material compositing" operation -- build a material
    // texture from a base colour plus placed art, which a mesh then samples.
    // Runs its own offscreen pass; must be inside a begin_frame/end_frame bracket.
    bool composite_texture_layers_dx12(
        Device& device,
        GPUHandle target,
        const float base_color[4],
        const TextureCompositeLayer* layers,
        std::size_t layer_count);

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
    struct VectorFieldData;
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

    GPUHandle upload_vector_field_texture_dx12(
        Device& device,
        const wz::engine::assets::VectorFieldData& field
    );

    const DX12ScalarFieldTexture* get_scalar_field_texture(
        Device& device,
        GPUHandle handle);

    ID3D12DescriptorHeap* get_scalar_field_srv_heap(Device& device);
}


// ── Generic texture (rhi-agnostic) ───────────────────────────────
//
// The generalization of the scalar-field texture path: a committed texture +
// upload with NO dedicated SRV heap (descriptors are bound at draw time via the
// SRG path). Backs the rhi GpuResourceRegistry's Texture2D/3D resources through
// engine/rendering/rhi_gpu_backend.

namespace wz::gpu {
    struct TextureDesc;
}

namespace wz::gpu::dx12::internal {

    struct DX12Texture
    {
        ID3D12Resource*       texture    = nullptr;
        uint32_t              width      = 0;
        uint32_t              height     = 0;
        uint32_t              depth      = 1;
        uint32_t              mip_levels = 1;
        DXGI_FORMAT           format     = DXGI_FORMAT_UNKNOWN;
        D3D12_RESOURCE_STATES state      = D3D12_RESOURCE_STATE_COMMON;

        // Offscreen render-to-texture (S6): when the texture was created renderable
        // it owns a 1-descriptor RTV heap + handle so a pass can bind it as a color
        // target; null/zero for a plain sampled texture. Released with the texture.
        ID3D12DescriptorHeap*       rtv_heap = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        bool                        is_render_target = false;

        bool valid() const noexcept
        {
            return texture != nullptr && width > 0u && height > 0u && depth > 0u;
        }
    };

    class DX12TextureTable
    {
    public:
        DX12TextureTable();

        GPUHandle add(DX12Texture tex);
        DX12Texture* get(GPUHandle handle);
        const DX12Texture* get(GPUHandle handle) const;
        bool release(GPUHandle handle);
        void destroy();

    private:
        struct Slot
        {
            uint32_t    epoch = 0;
            bool        occupied = false;
            DX12Texture tex{};
        };

        std::vector<Slot> slots_;
    };

    GPUHandle create_texture_dx12(
        Device& device,
        const wz::gpu::TextureDesc& desc);

    bool update_texture_dx12(
        Device& device,
        GPUHandle handle,
        const void* data,
        uint64_t byte_count,
        uint64_t byte_offset = 0);

    // Upload tightly-packed bytes into one mip level (subresource = mip_level)
    // of a texture, honoring the device upload row pitch. byte_count must equal
    // the level's tightly-packed footprint (max(w>>mip,1) * max(h>>mip,1) *
    // depth * texel_bytes). Leaves the texture in a shader-resource state.
    bool update_texture_mip_dx12(
        Device& device,
        GPUHandle handle,
        uint32_t mip_level,
        const void* data,
        uint64_t byte_count);

    bool release_texture_dx12(Device& device, GPUHandle handle);

    // Test/diagnostic accessor: the backing DX12Texture for a handle minted by
    // create_texture_dx12, or nullptr for a stale/foreign handle. Exposes the
    // resolved mip count + resource for coverage of the #209 mip path.
    const DX12Texture* get_dx12_texture(Device& device, GPUHandle handle);
}



// ── Mesh buffers ──────────────────────────────────────────────────

namespace wz::engine::assets {
    struct MeshData;
    struct MeshDerivedFieldData;
}

namespace wz::gpu {
    struct MeshFieldVisualizationUploadDesc;
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
        bool release(GPUHandle handle);
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

    bool release_mesh_dx12(
        Device& device,
        GPUHandle handle
    );

    struct DX12MeshFieldVisualizationResource
    {
        ID3D12Resource* values_buffer = nullptr;
        uint32_t element_count = 0;
        uint32_t stride_bytes = 0;
        // True for DEFAULT-heap buffers created from a GPU source; these can
        // be refreshed in place via update_mesh_field_visualization_dx12.
        // CPU-uploaded resources live on an UPLOAD heap and cannot be a GPU
        // copy destination.
        bool gpu_updatable = false;
        wz::gpu::dx12::DX12DescriptorTable srv_table{};

        bool valid() const noexcept
        {
            return values_buffer != nullptr
                && element_count > 0u
                && srv_table.valid();
        }
    };

    class DX12MeshFieldVisualizationTable
    {
    public:
        DX12MeshFieldVisualizationTable();

        GPUHandle add(DX12MeshFieldVisualizationResource resource);
        const DX12MeshFieldVisualizationResource* get(
            GPUHandle handle) const;
        bool release(
            GPUHandle handle,
            wz::gpu::dx12::DX12DescriptorAllocator& allocator);
        void destroy(wz::gpu::dx12::DX12DescriptorAllocator& allocator);

    private:
        struct Slot
        {
            uint32_t epoch = 0;
            bool occupied = false;
            DX12MeshFieldVisualizationResource resource{};
        };

        std::vector<Slot> slots_;
    };

    GPUHandle upload_mesh_field_visualization_dx12(
        Device& device,
        const wz::gpu::MeshFieldVisualizationUploadDesc& desc);

    GPUHandle upload_mesh_field_visualization_values_dx12(
        Device& device,
        const std::byte* values,
        uint64_t value_byte_count,
        uint32_t element_count,
        uint32_t stride_bytes);

    bool update_mesh_field_visualization_values_dx12(
        Device& device,
        GPUHandle destination,
        const std::byte* values,
        uint64_t value_byte_count,
        uint32_t element_count,
        uint32_t stride_bytes);

    GPUHandle create_mesh_field_visualization_from_gpu_source_dx12(
        Device& device,
        GPUHandle source_buffer,
        uint64_t byte_offset,
        uint32_t element_count,
        uint32_t stride_bytes);

    bool update_mesh_field_visualization_from_gpu_source_dx12(
        Device& device,
        GPUHandle destination,
        GPUHandle source_buffer,
        uint64_t byte_offset,
        uint32_t element_count,
        uint32_t stride_bytes);

    const DX12MeshFieldVisualizationResource*
    get_mesh_field_visualization(
        Device& device,
        GPUHandle handle);

    bool release_mesh_field_visualization_dx12(
        Device& device,
        GPUHandle handle);
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

    // Scene-wide splat coverage settings, last pushed by
    // wz::gpu::set_splat_coverage_settings().  Default is TransparentBlend.
    const wz::gpu::SplatCoverageSettings& get_coverage_settings(Device& device);
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
// Stores PSO + root signature pairs created by RenderProgramPipelineCache.
// Keyed by GPUHandle of type kGPUGraphicsPipelineResourceType.
// The debug context singletons are separate; this table is for the
// production pipeline cache path.

#include <engine/assets/renderable/renderable.h>

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

namespace wz::gpu {
    struct ComputeBufferDesc;
    struct ComputeDispatchDesc;
}

namespace wz::gpu::dx12::internal {

    struct DX12ComputeBuffer
    {
        ID3D12Resource* resource = nullptr;
        uint32_t element_count = 0;
        uint32_t stride_bytes = 0;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

        bool valid() const noexcept
        {
            return resource != nullptr
                && element_count > 0u
                && stride_bytes > 0u;
        }
    };

    class DX12ComputeBufferTable
    {
    public:
        DX12ComputeBufferTable();

        GPUHandle add(DX12ComputeBuffer buffer);
        DX12ComputeBuffer* get(GPUHandle handle);
        const DX12ComputeBuffer* get(GPUHandle handle) const;
        bool release(GPUHandle handle);
        void destroy();

    private:
        struct Slot
        {
            uint32_t epoch = 0;
            bool occupied = false;
            DX12ComputeBuffer buffer{};
        };

        std::vector<Slot> slots_;
    };

    struct DX12ComputePipeline
    {
        ID3D12RootSignature* root_sig = nullptr;
        ID3D12PipelineState* pso = nullptr;
        wz::engine::assets::ComputePipelineData data{};

        bool valid() const noexcept { return root_sig && pso && data.valid(); }
    };

    class DX12ComputePipelineTable
    {
    public:
        DX12ComputePipelineTable();

        GPUHandle add(DX12ComputePipeline pipeline);
        const DX12ComputePipeline* get(GPUHandle handle) const;
        bool release(GPUHandle handle);
        void destroy();

    private:
        struct Slot
        {
            uint32_t epoch = 0;
            bool occupied = false;
            DX12ComputePipeline pipeline{};
        };

        std::vector<Slot> slots_;
    };

    GPUHandle create_structured_buffer_dx12(
        Device& device,
        const wz::gpu::ComputeBufferDesc& desc,
        bool allow_unordered_access);

    GPUHandle create_compute_pipeline_dx12(
        Device& device,
        const wz::engine::assets::ComputePipelineData& data,
        GPUHandle compute_shader);

    bool dispatch_compute_dx12(Device& device, const wz::gpu::ComputeDispatchDesc& desc);
    std::vector<std::byte> readback_buffer_dx12(Device& device, GPUHandle buffer);
    bool update_compute_buffer_dx12(
        Device& device,
        GPUHandle destination,
        const void* data,
        uint64_t byte_count,
        uint64_t byte_offset = 0);
    // Record the update INTO THE FRAME'S command list instead of the immediate
    // one-shot above: the new bytes are visible to draws recorded AFTER this
    // call, while draws recorded BEFORE it still read the previous content.
    // Required for any buffer refreshed more than once per frame with
    // different values — with the immediate copy every recorded draw reads the
    // LAST refresh at execute (#311 B2-S1). Staging rides a per-frame arena
    // released at the next begin_frame. Call only between begin/end_frame.
    bool record_compute_buffer_update_dx12(
        Device& device,
        GPUHandle destination,
        const void* data,
        uint64_t byte_count,
        uint64_t byte_offset = 0);
    bool release_compute_buffer_dx12(Device& device, GPUHandle handle);
    bool release_compute_pipeline_dx12(Device& device, GPUHandle handle);

    bool transition_compute_buffer_for_graphics_srv(
        Device& device,
        ID3D12GraphicsCommandList* cmd,
        GPUHandle buffer);

    bool transition_compute_buffer(
        Device& device,
        ID3D12GraphicsCommandList* cmd,
        GPUHandle buffer,
        D3D12_RESOURCE_STATES after);

    bool uav_barrier_compute_buffer(
        Device& device,
        ID3D12GraphicsCommandList* cmd,
        GPUHandle buffer);

    bool create_compute_buffer_srv_table(
        Device& device,
        std::span<const GPUHandle> buffers,
        wz::gpu::dx12::DX12DescriptorTable& out_table);

    bool create_compute_buffer_descriptor_table(
        Device& device,
        std::span<const GPUHandle> buffers,
        std::span<const uint8_t> unordered_access,
        wz::gpu::dx12::DX12DescriptorTable& out_table);

    // Per-descriptor view kind for the generic SRG descriptor-table builder
    // below. gpu-local (this layer stays rhi-agnostic, like ComputeBindingKind);
    // the rhi command recorder maps wz::rhi::DescriptorKind onto this. Each value
    // selects which device table the handle is resolved in and which view is
    // written: buffer kinds use the compute_buffers table + structured-buffer
    // view, Texture2DSRV uses the textures table + a typed Texture2D/3D SRV.
    enum class DescriptorViewKind : uint8_t
    {
        StructuredBufferSRV,
        StructuredBufferUAV,
        Texture2DSRV,
    };

    // Generic SRG descriptor-table builder: handles both buffer and texture
    // resources in one table, picking per slot from `kinds`. This is the
    // texture-aware generalization of create_compute_buffer_descriptor_table
    // (which only knows structured buffers); the rhi render path uses it so a
    // TextureSRV descriptor binds a resident Texture2D alongside pull buffers.
    // `handles` and `kinds` must be the same length and non-empty.
    bool create_resource_descriptor_table(
        Device& device,
        std::span<const GPUHandle> handles,
        std::span<const DescriptorViewKind> kinds,
        wz::gpu::dx12::DX12DescriptorTable& out_table);

    void release_compute_buffer_srv_table(
        Device& device,
        const wz::gpu::dx12::DX12DescriptorTable& table);
}
