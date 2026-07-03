#pragma once

// engine/assets/render_binding_layout/render_binding_layout.h
//
// Authored render binding layout (issue #227): the shared SRG shapes that the
// numbered `binding_layout` presets 0–4 hard-coded, promoted to a zero-dep
// asset type. A layout declares one optional root-constant block (a named
// head packer plus authored tail field declarations), ordered SRV binding
// rows, and ordered static-sampler rows. Registers are DERIVED from row order
// (b0 / t0,t1… / s0,s1… in the object register space) — never authored.

#include <asset/types.h>
#include <engine/assets/render_program/render_program.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wz::engine::assets
{
    // Authoring caps for the indexed param rows (binding0..7, const0..7,
    // sampler0..1) — the scalar-param encoding of the layout tables.
    inline constexpr uint32_t kMaxRenderBindingLayoutBindings = 8u;
    inline constexpr uint32_t kMaxRenderBindingLayoutConstantFields = 8u;
    inline constexpr uint32_t kMaxRenderBindingLayoutSamplers = 2u;

    // Every authored-layout register lives in the object SRG register space,
    // matching presets 1–4 (the space RhiSceneRenderer binds per draw).
    inline constexpr uint32_t kRenderBindingLayoutRegisterSpace = 2u;

    // Head of the root-constant block: names one of the EXISTING renderer
    // packers, so the first N dwords of the block are filled by known code
    // (mvp16 → the 16-float MVP, world_viewproj_camera36 → SplatCloud-style
    // world[16]+view_proj[16]+camera[4], clipmap32 → ClipmapDrawConstants).
    enum class RenderBindingConstantsHead : uint8_t
    {
        None,
        Mvp16,
        WorldViewProjCamera36,
        Clipmap32,
    };

    [[nodiscard]] constexpr uint32_t render_binding_constants_head_dwords(
        RenderBindingConstantsHead head) noexcept
    {
        switch (head) {
        case RenderBindingConstantsHead::None:                  return 0u;
        case RenderBindingConstantsHead::Mvp16:                 return 16u;
        case RenderBindingConstantsHead::WorldViewProjCamera36: return 36u;
        case RenderBindingConstantsHead::Clipmap32:             return 32u;
        }
        return 0u;
    }

    enum class RenderBindingConstantType : uint8_t
    {
        Float,
        Float2,
        Float3,
        Float4,
        Color,
    };

    [[nodiscard]] constexpr uint32_t render_binding_constant_type_dwords(
        RenderBindingConstantType type) noexcept
    {
        switch (type) {
        case RenderBindingConstantType::Float:  return 1u;
        case RenderBindingConstantType::Float2: return 2u;
        case RenderBindingConstantType::Float3: return 3u;
        case RenderBindingConstantType::Float4: return 4u;
        case RenderBindingConstantType::Color:  return 4u;
        }
        return 0u;
    }

    // Authored tail field DECLARATION (name + type only). Field defaults and
    // values live at consumption sites — renderable node params and scene-node
    // overrides (#228/#229) — never on the layout.
    struct RenderBindingConstantField
    {
        std::string name;
        RenderBindingConstantType type = RenderBindingConstantType::Float;
    };

    enum class RenderBindingKind : uint8_t
    {
        TextureSrv,
        StructuredSrv,
    };

    struct RenderBindingRow
    {
        // Canonical descriptor-semantic name (kDescriptorSemanticNames).
        std::string semantic;
        RenderBindingKind kind = RenderBindingKind::TextureSrv;
        ShaderVisibility visibility = ShaderVisibility::All;
    };

    struct RenderBindingSamplerRow
    {
        StaticSamplerKind kind = StaticSamplerKind::LinearClamp;
        ShaderVisibility visibility = ShaderVisibility::All;
    };

    struct RenderBindingLayoutData
    {
        // Root-constant block — at most one, present iff constants_semantic is
        // non-empty (b0 in the object space when present).
        std::string constants_semantic;
        ShaderVisibility constants_visibility = ShaderVisibility::All;
        RenderBindingConstantsHead constants_head =
            RenderBindingConstantsHead::None;
        // Authored total block size in dwords; 0 derives head + tail. When
        // authored, it must cover head + tail (trailing padding is allowed).
        uint32_t constants_dwords = 0;
        std::vector<RenderBindingConstantField> constant_fields;

        // SRV rows; registers derive from row order (t0, t1, …).
        std::vector<RenderBindingRow> bindings;
        // Static-sampler rows; registers derive from row order (s0, s1).
        std::vector<RenderBindingSamplerRow> samplers;

        [[nodiscard]] bool has_constants() const noexcept
        {
            return !constants_semantic.empty();
        }

        [[nodiscard]] uint32_t tail_dwords() const noexcept;

        // The block size the derived SRG uses: authored constants_dwords when
        // non-zero, otherwise head + tail.
        [[nodiscard]] uint32_t total_constants_dwords() const noexcept;

        [[nodiscard]] bool valid() const noexcept;
    };

    struct RenderBindingLayoutCompileDesc
    {
        RenderBindingLayoutData layout{};
    };

    // The program-facing SRG derived from a layout: registers assigned by row
    // order in the object space. This is THE one derivation — the custom
    // render-program compiler consumes it, and the generated HLSL binding
    // include (#231) must mirror it.
    struct RenderBindingLayoutSrg
    {
        std::vector<RootConstantBinding>  root_constants;
        std::vector<DescriptorBinding>    descriptor_bindings;
        std::vector<StaticSamplerBinding> static_samplers;
    };

    // Returns false and fills `error` when the layout cannot produce a valid
    // SRG (unknown/duplicate semantic name, undersized constant block, …).
    [[nodiscard]] bool build_render_binding_layout_srg(
        const RenderBindingLayoutData& layout,
        RenderBindingLayoutSrg& out,
        std::string& error);

    class RenderBindingLayoutTable
    {
    public:
        RenderBindingLayoutTable();

        wz::asset::ResourceHandle add(RenderBindingLayoutData data);
        const RenderBindingLayoutData* get(
            wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        std::vector<RenderBindingLayoutData> layouts_;
        std::vector<uint32_t> epochs_;
    };
}
