#pragma once

// engine/assets/render_program/render_program.h

#include <asset/types.h>
#include <engine/assets/render_binding_layout/render_binding_constants.h>
#include <engine/assets/renderable/renderable.h>

// wz::rhi::BlendMode — the engine consumes the single rhi blend enum directly
// rather than duplicating it (issue #272).
#include <wozzits/rhi/render_program.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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

    // BlendMode is wz::rhi::BlendMode (issue #272): the engine consumes the one
    // rhi blend enum directly, so the fields below spell it `wz::rhi::BlendMode`.
    // DepthMode / RasterMode remain duplicated here (bridged in
    // rhi_render_program_bridge.cpp) pending their own consolidation.

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
        SkyGaussian,
        // Appended (not inserted) to keep existing enum values stable for any
        // baked binding-layout recipes. Per-vertex mesh normals, pulled the same
        // way as positions/indices; published by gpu_sparse_mesh_compilers.cpp.
        PulledMeshNormals,
        // The SG sky's high-frequency point sources (sun/moon/stars), a second
        // resident buffer alongside the "sky_gaussian" dome lobes (source B).
        SkyGaussianPoints,
        // A star catalog's resident point buffer (source C, #266), bound by the
        // star-field render branch and expanded to instanced billboards.
        StarCatalog,
        // VIEW-frequency constants (register space 0): the observer and the
        // atmosphere it looks through, identical for every program in a frame.
        // A StructuredBuffer rather than a cbuffer because the DX12 pipeline
        // realizes exactly ONE root-constant block per program and the object
        // block already claims it -- see wozzits-rhi#7. Descriptor tables are
        // already plural, so this needs nothing from that layer.
        ViewConstants,
        // VIEW-frequency SCREEN constants (register space 0): the viewport size,
        // for screen-space passes (2D overlays / HUD) that place geometry in
        // pixels. A distinct semantic from ViewConstants so an overlay's Screen
        // head and a 3D program's CameraFog head coexist in one frame, each bound
        // to its own per-frame buffer.
        ScreenConstants,
        // A generic imported-image texture bound to a renderable (kAssetTypeTexture,
        // "texture" variant) -- the 2D overlay's sprite. A distinct semantic so the
        // overlay layout names its own row, separate from field/vector textures.
        OverlayTexture,
        // An Inochi2D puppet Part's pull streams + atlas (screen-space 2D art),
        // bound per Part by the puppet render branch: PuppetVertices = interleaved
        // (pos,uv) StructuredBuffer, PuppetIndices = u32 StructuredBuffer,
        // PuppetAtlas = the Part's atlas-page texture.
        PuppetVertices,
        PuppetIndices,
        PuppetAtlas,
        // A mesh's albedo/base-colour material texture, sampled in the surface
        // shader. Distinct from OverlayTexture (a 2D sprite drawn as itself):
        // this one is a SURFACE input, and it is what the material compositor
        // writes -- a base colour plus placed art (labels, decals, a puppet)
        // built into one texture the lit shader samples through the mesh's UV.
        MaterialAlbedo,
        // The coverage mask clipping an Inochi2D puppet Part (#275): the mask
        // SOURCE Part rendered alone into a target-sized texture, whose ALPHA is
        // the coverage the masked Part's pixel shader tests against its
        // mask_threshold. Every puppet Part declares this row -- an UNMASKED one
        // binds a 1x1 white texture -- so masking needs no second program set.
        PuppetMask,
        // Per-vertex texture coordinates, pulled the same way as positions /
        // indices / normals (#290). Published only when the source mesh carries
        // them; a program that declares this row therefore REQUIRES a UV-bearing
        // mesh, exactly as the normals row requires normals. Without it a
        // material texture could only be mapped by deriving a UV from geometry,
        // which is why the lit sphere derives an equirectangular one -- correct
        // for a sphere and meaningless on anything else.
        PulledMeshUvs,
    };

    // Canonical open-vocabulary names for DescriptorSemantic. ONE table serves
    // both directions: the rhi bridge projects the enum to a name and registers
    // it as a Tag (enum → name), and authored render-binding-layout rows name
    // their semantic as a string that resolves back to the enum (name → enum).
    // Index == enum value; a new DescriptorSemantic member extends this array.
    inline constexpr std::array<std::string_view, 22> kDescriptorSemanticNames = {
        "unknown",
        "splat_cloud",
        "sorted_splat_indices",
        "scalar_field_texture",
        "mesh_field_visualization",
        "mesh_mask_rules",
        "pulled_mesh_positions",
        "pulled_mesh_indices",
        "pulled_mesh_source_vertices",
        "sky_gaussian",
        "pulled_mesh_normals",
        "sky_gaussian_points",
        "star_catalog",
        "view_constants",
        "screen_constants",
        "overlay_texture",
        "puppet_vertices",
        "puppet_indices",
        "puppet_atlas",
        "material_albedo",
        "puppet_mask",
        "pulled_mesh_uvs",
    };

    [[nodiscard]] constexpr std::string_view descriptor_semantic_name(
        DescriptorSemantic semantic) noexcept
    {
        const size_t index = static_cast<size_t>(semantic);
        if (index < kDescriptorSemanticNames.size()) {
            return kDescriptorSemanticNames[index];
        }
        return kDescriptorSemanticNames[0];
    }

    // "unknown" is not authorable: it names the absent state, so parsing it (or
    // any unrecognized name) fails rather than producing a binding the renderer
    // could never fill.
    [[nodiscard]] constexpr std::optional<DescriptorSemantic>
    descriptor_semantic_from_name(std::string_view name) noexcept
    {
        for (size_t i = 1; i < kDescriptorSemanticNames.size(); ++i) {
            if (kDescriptorSemanticNames[i] == name) {
                return static_cast<DescriptorSemantic>(i);
            }
        }
        return std::nullopt;
    }

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
        // Optional authored binding layout (issue #227), dep[2] when non-empty.
        // When set, the layout asset defines the whole SRG at compile time and
        // the root_constants/descriptor_bindings/static_samplers vectors below
        // are ignored.
        wz::asset::AssetKey binding_layout{};

        RenderBindingModel    binding_model{};
        RenderPrimitiveTopology topology = RenderPrimitiveTopology::TriangleList;

        RenderDomain default_domain{};
        uint32_t default_policy_flags = RenderPolicy_None;

        InputLayoutKind input_layout{};
        wz::rhi::BlendMode blend_mode{};
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
        wz::rhi::BlendMode blend_mode{};
        DepthMode       depth_mode{};
        RasterMode      raster_mode{};

        // Root signature layout in declaration order.
        // root_constants[i] maps to DX12 root parameter i.
        // descriptor_bindings follow at indices [root_constants.size(), ...).
        std::vector<RootConstantBinding> root_constants;
        std::vector<DescriptorBinding>   descriptor_bindings;
        std::vector<StaticSamplerBinding> static_samplers;

        // The authored-layout constants contract (issue #228), copied from the
        // wired render-binding-layout when a 0x103 program compiles with one.
        // has_authored_binding_layout distinguishes "layout-authored, empty
        // constants" from a numbered-preset program (whose head/fields are
        // renderer conventions this struct does not describe); the custom
        // renderable compiler (0x70A) requires it, validates authored constant
        // names against constant_fields, and selects the head packer from
        // constants_head. Preset/builtin programs leave all three at defaults.
        bool has_authored_binding_layout = false;
        RenderBindingConstantsHead constants_head =
            RenderBindingConstantsHead::None;
        std::vector<RenderBindingConstantField> constant_fields;

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
