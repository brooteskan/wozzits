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

    enum class ShaderStage : uint8_t { All, Vertex, Pixel };

    enum class BindingModel : uint8_t
    {
        MeshIA,
        SplatVertexInstanced,
        SplatPull,
        ScalarFieldTexture,
        Fullscreen,
        ParticlePull,
    };

    enum class PrimitiveTopology : uint8_t { TriangleList, TriangleStrip };

    enum class InputLayout : uint8_t
    {
        None,
        MeshPositionOnly,
        MeshPositionNormalUV,
        GaussianSplatVertex,
    };

    enum class BlendMode : uint8_t { Opaque, AlphaBlend };
    enum class DepthMode : uint8_t { Disabled, TestNoWrite, TestWrite };
    enum class RasterMode : uint8_t { SolidCullBack, SolidCullNone, WireframeCullNone };
    enum class DescriptorKind : uint8_t { StructuredBufferSRV, TextureSRV, Sampler, UAV };

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

        BindingModel      binding_model = BindingModel::MeshIA;
        PrimitiveTopology topology      = PrimitiveTopology::TriangleList;
        InputLayout       input_layout  = InputLayout::MeshPositionNormalUV;
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
