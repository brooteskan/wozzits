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
#include <engine/assets/render_binding_layout/render_binding_constants.h>
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

    // View-frequency register space. The slot convention is view=0, material=1,
    // object=2, and the rhi bridge derives one SRG per register space, so rows
    // stamped here become a binding_slot 0 group with no bridge change.
    inline constexpr uint32_t kRenderBindingLayoutViewRegisterSpace = 0u;

    // RenderBindingConstantsHead / RenderBindingConstantType /
    // RenderBindingConstantField moved to render_binding_constants.h (a leaf
    // header) so render_program.h and renderable.h can carry the constants
    // contract too (issue #228). Same namespace — call sites are unchanged.

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

        // View-frequency state (register space 0): the observer, and the
        // atmosphere it looks through -- identical for every program in a frame.
        // None = the layout wants none of it, and derives byte-identically to a
        // layout authored before this existed.
        //
        // Delivered as a one-element STRUCTURED BUFFER, not a cbuffer. The DX12
        // pipeline realizes exactly one root-constant block per program and the
        // object block already claims it; a second makes pipeline realization
        // fail outright (wozzits-rhi#7). Descriptor tables are already plural,
        // so a buffer costs that layer nothing.
        //
        // Head-only by design, unlike the object block above. The object tail
        // exists so a recipe can carry per-INSTANCE parameters; view state has
        // no per-instance variation by definition, so an authored tail here
        // would only be a way to disagree with what the renderer fills.
        RenderBindingViewHead view_head = RenderBindingViewHead::None;
        ShaderVisibility view_visibility = ShaderVisibility::All;

        // SRV rows; registers derive from row order (t0, t1, …).
        std::vector<RenderBindingRow> bindings;
        // Static-sampler rows; registers derive from row order (s0, s1).
        std::vector<RenderBindingSamplerRow> samplers;

        [[nodiscard]] bool has_constants() const noexcept
        {
            return !constants_semantic.empty();
        }

        [[nodiscard]] bool has_view_constants() const noexcept
        {
            return view_head != RenderBindingViewHead::None;
        }

        [[nodiscard]] uint32_t view_constants_dwords() const noexcept
        {
            return render_binding_view_head_dwords(view_head);
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

    // The program-facing SRG derived from a layout. Descriptor registers are
    // assigned by canonical SEMANTIC order in the object space, not by the row
    // order the author wrote (#283), so reordering rows produces an identical
    // SRG. This is THE one derivation — the custom render-program compiler
    // consumes it, and the generated HLSL binding include (#231) must mirror it.
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

    // The shader-side view of the same derivation (issue #231): HLSL
    // declarations for everything the layout authors — the cbuffer (head +
    // tail fields at their packed offsets, emitted as packoffset so the byte
    // contract is forced, not inferred), the SRV rows (canonical element
    // types where a semantic has one, a WZ_BINDING_<SEMANTIC> register macro
    // where the author must declare their own), and the static samplers
    // (sampler0/sampler1, matching the authoring row names). The 0x105
    // binding-prelude compiler prepends this to an authored shader body, so
    // shaders never hand-declare registers or offsets the layout owns.
    // Returns false and fills `error` on the same conditions as
    // build_render_binding_layout_srg (it is this derivation, re-emitted).
    [[nodiscard]] bool generate_hlsl_binding_prelude(
        const RenderBindingLayoutData& layout,
        std::string& out,
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
