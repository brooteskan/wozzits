#pragma once

// engine/assets/render_program/render_program.h

#include <asset/types.h>
#include <engine/assets/renderable/renderable.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wz::engine::assets
{
    enum class ShaderVisibility : uint8_t
    {
        All,
        Vertex,
        Pixel,
    };

    enum class RenderBindingModel : uint8_t
    {
        MeshIA,
        SplatVertexInstanced,
        SplatPull,
        MeshVertexPull,
        ScalarFieldTexture,
        Fullscreen,
        ParticlePull,
    };

    enum class RenderPrimitiveTopology : uint8_t
    {
        TriangleList,
        TriangleStrip,
    };

    // ── Declarative pipeline description enums ────────────────────────────────

    enum class InputLayoutKind : uint8_t
    {
        None,               // pull-based or fullscreen — no IA vertex buffer
        MeshPositionOnly,   // float3 POSITION, per-vertex
        MeshPositionNormalUV, // POSITION + NORMAL + TEXCOORD0 from MeshVertex
        GaussianSplatVertex, // per-instance: position, opacity, scale, rotation, color
    };

    enum class BlendMode : uint8_t
    {
        Opaque,
        AlphaBlend,
    };

    enum class DepthMode : uint8_t
    {
        Disabled,
        TestNoWrite,
        TestWrite,
    };

    enum class RasterMode : uint8_t
    {
        SolidCullBack,
        SolidCullNone,
        WireframeCullNone,
    };

    // ── Declarative binding structs ───────────────────────────────────────────

    struct RootConstantBinding
    {
        ShaderVisibility visibility{};
        uint32_t shader_register = 0;
        uint32_t register_space  = 0;
        uint32_t value_count     = 0;

        // Open-vocabulary name for this constant range (e.g. "world",
        // "view_proj"), sourced from semantic_names.h. The rhi bridge resolves
        // it to a ConstantSemantic Tag at the boundary; an rhi Tag is never
        // stored in asset data.
        std::string semantic;
    };

    enum class DescriptorKind : uint8_t
    {
        StructuredBufferSRV,
        TextureSRV,
        Sampler,
        UAV,
    };

    // Semantic identifies the logical role of a bound resource so the submit
    // path can locate the right GPU handle without hard-coding slot numbers.
    enum class DescriptorSemantic : uint8_t
    {
        Unknown,
        SplatCloud,
        SortedSplatIndices,
        ScalarFieldTexture,
        MeshFieldVisualization,
        MeshMaskRules,
        PulledMeshPositions,
        PulledMeshIndices,
        PulledMeshSourceVertices,
    };

    struct DescriptorBinding
    {
        DescriptorKind     kind{};
        ShaderVisibility   visibility{};
        DescriptorSemantic semantic{};
        uint32_t shader_register  = 0;
        uint32_t register_space   = 0;
        uint32_t descriptor_count = 1;
    };

    // A closed set of well-known sampler recipes baked into the root signature.
    // Mirrors wz::rhi::StaticSamplerKind; the bridge maps 1:1.
    enum class StaticSamplerKind : uint8_t
    {
        LinearClamp,
        LinearWrap,
    };

    // A static sampler declared on a program. It occupies a sampler register
    // (s#) in register_space but consumes no descriptor slot — the DX12 backend
    // bakes it into the root signature. Used, e.g., for the clipmap height tap.
    struct StaticSamplerBinding
    {
        StaticSamplerKind kind{};
        ShaderVisibility  visibility{};
        uint32_t shader_register = 0;
        uint32_t register_space  = 0;
    };

    // dep[0] = vertex_shader key, dep[1] = pixel_shader key.
    struct BuiltinRenderProgramDesc
    {
        std::string name;
        BuiltinRenderProgram program{};
        wz::asset::AssetKey vertex_shader{};
        wz::asset::AssetKey pixel_shader{};
    };

    // dep[0] = vertex_shader key, dep[1] = pixel_shader key.
    // All pipeline state is explicitly authored — no builtin enum lookup.
    struct CustomRenderProgramDesc
    {
        std::string name;
        wz::asset::AssetKey vertex_shader{};
        wz::asset::AssetKey pixel_shader{};

        RenderBindingModel    binding_model{};
        RenderPrimitiveTopology topology = RenderPrimitiveTopology::TriangleList;

        RenderDomain default_domain{};
        uint32_t default_policy_flags = RenderPolicy_None;

        InputLayoutKind input_layout{};
        BlendMode       blend_mode{};
        DepthMode       depth_mode{};
        RasterMode      raster_mode{};

        std::vector<RootConstantBinding> root_constants;
        std::vector<DescriptorBinding>   descriptor_bindings;
        std::vector<StaticSamplerBinding> static_samplers;
    };

    struct RenderProgramData
    {
        BuiltinRenderProgram builtin_program{};

        RenderBindingModel    binding_model{};
        RenderPrimitiveTopology topology{};

        RenderDomain default_domain{};
        uint32_t default_policy_flags = RenderPolicy_None;

        // Declarative pipeline state — read by DX12 factory in Phase 2+.
        InputLayoutKind input_layout{};
        BlendMode       blend_mode{};
        DepthMode       depth_mode{};
        RasterMode      raster_mode{};

        // Root signature layout in declaration order.
        // root_constants[i] maps to DX12 root parameter i.
        // descriptor_bindings follow at indices [root_constants.size(), ...).
        std::vector<RootConstantBinding> root_constants;
        std::vector<DescriptorBinding>   descriptor_bindings;
        std::vector<StaticSamplerBinding> static_samplers;

        wz::asset::ResourceHandle vertex_shader{};
        wz::asset::ResourceHandle pixel_shader{};

        bool valid() const noexcept
        {
            return vertex_shader.valid() && pixel_shader.valid();
        }
    };

    class RenderProgramTable
    {
    public:
        RenderProgramTable();

        wz::asset::ResourceHandle add(RenderProgramData data);
        const RenderProgramData* get(wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        std::vector<RenderProgramData> programs_;
        std::vector<uint32_t> epochs_;
    };
}
