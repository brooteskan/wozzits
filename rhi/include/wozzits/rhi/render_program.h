#pragma once

// wozzits/rhi/render_program.h
//
// The declarative render-program description: the data contract at the engine
// boundary. A render program is fully described by this struct — no behavior
// hides in a switch keyed off an enum member.
//
// Note which things are enums and which are not, because it is the whole point:
//
//   - Pipeline STATE (blend / depth / raster / input layout / topology /
//     binding model) is a set of CLOSED, bounded value sets. Those stay enums;
//     the correct safety tool for them is exhaustiveness checking, not a
//     registry. Adding a member should break a switch at compile time.
//
//   - Descriptor SEMANTIC (which logical resource a binding wants) is an OPEN
//     identity set: each new render path may introduce a new one. In the old
//     engine this was `DescriptorSemantic`, an enum that grew over time
//     (... MeshFieldVisualization, MeshMaskRules) and had to be agreed upon by
//     many call sites — exactly the open-identity enum this repo refuses. Here
//     it is a Tag, registered by name like a render program.

#include <wozzits/rhi/tag_registry.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wz::rhi
{
    // ── Closed, bounded pipeline-state value sets: correctly enums ────────────

    enum class ShaderStage : uint8_t { All, Vertex, Pixel, Compute };

    // How the pipeline sources vertices. A small, closed STRUCTURAL set — not
    // content names. The engine's BindingModel had MeshIA / SplatVertexInstanced
    // / ScalarFieldTexture / ParticlePull / ... — those name the *renderables*
    // that use a strategy, an open set that grew per content. The structural
    // truth is just these three; instancing rides on per-attribute step rate.
    enum class VertexSource : uint8_t
    {
        InputAssembler,   // bind vertex/index buffers (classic IA)
        Pull,             // no IA; the vertex shader reads from an SRV by index
        None,             // no vertex input (fullscreen / fully procedural)
    };

    enum class PrimitiveTopology : uint8_t { TriangleList, TriangleStrip };

    // Vertex attribute format — closed, API-bounded enum (like BlendMode /
    // TextureFormat). Content uses existing formats; it cannot invent new ones.
    enum class VertexFormat : uint8_t
    {
        Float32,
        Float32x2,
        Float32x3,
        Float32x4,
        UInt32,
        UInt8x4Unorm,
    };

    enum class VertexStepRate : uint8_t { PerVertex, PerInstance };

    enum class BlendMode : uint8_t { Opaque, AlphaBlend };
    enum class DepthMode : uint8_t { Disabled, TestNoWrite, TestWrite };
    enum class RasterMode : uint8_t { SolidCullBack, SolidCullNone, WireframeCullNone };
    enum class DescriptorKind : uint8_t { StructuredBufferSRV, TextureSRV, Sampler, UAV };

    // ── Vertex input (data, not an enum) ───────────────────────────────────────

    struct VertexAttribute
    {
        uint32_t       location    = 0;   // shader input location / semantic slot
        VertexFormat   format      = VertexFormat::Float32x3;
        uint32_t       offset      = 0;   // byte offset within its buffer slot
        uint32_t       buffer_slot = 0;
        VertexStepRate step        = VertexStepRate::PerVertex;
    };

    // The vertex input layout as DATA. A new vertex format — a new renderable's
    // geometry — is a different attribute list, NOT a new enum member plus a new
    // pipeline-factory switch. This is the D3D12_INPUT_ELEMENT_DESC[] / Vulkan
    // vertex-input model. Empty for VertexSource::Pull / None.
    struct VertexLayout
    {
        std::vector<VertexAttribute> attributes;
    };

    // ── Binding declarations ──────────────────────────────────────────────────

    struct RootConstantBinding
    {
        ShaderStage visibility = ShaderStage::All;
        uint32_t shader_register = 0;
        uint32_t register_space = 0;
        uint32_t value_count = 0;
    };

    struct DescriptorBinding
    {
        DescriptorKind kind = DescriptorKind::StructuredBufferSRV;
        ShaderStage    visibility = ShaderStage::Pixel;

        // The OPEN identity: which logical resource this binding wants. A Tag
        // from a DescriptorSemanticRegistry, never an enum. A binding whose
        // semantic resolves to a null Tag is a checkable error at bind time,
        // not a silent default.
        Tag semantic{};

        uint32_t shader_register = 0;
        uint32_t register_space = 0;
        uint32_t descriptor_count = 1;
    };

    // ── The program description (the boundary contract) ───────────────────────

    struct RenderProgramDesc
    {
        std::string name;

        // Shader identity as a resolvable ref (path or symbolic id). rhi's
        // shader subsystem resolves these; the engine adapter supplies them
        // from its shader AssetKeys.
        std::string vertex_shader;
        std::string pixel_shader;

        VertexSource      vertex_source = VertexSource::InputAssembler;
        VertexLayout      vertex_layout{};   // data; empty for Pull / None
        PrimitiveTopology topology      = PrimitiveTopology::TriangleList;
        BlendMode         blend_mode    = BlendMode::Opaque;
        DepthMode         depth_mode    = DepthMode::TestWrite;
        RasterMode        raster_mode   = RasterMode::SolidCullBack;

        // Root signature layout in declaration order.
        std::vector<RootConstantBinding> root_constants;
        std::vector<DescriptorBinding>   descriptor_bindings;
    };

    // Descriptor semantics are registered by name, exactly like render
    // programs. The adapter (or test) acquires a Tag per semantic and stamps it
    // into DescriptorBinding::semantic.
    inline constexpr size_t kMaxDescriptorSemantics = 128;
    using DescriptorSemanticRegistry = TagRegistry<kMaxDescriptorSemantics>;
}
