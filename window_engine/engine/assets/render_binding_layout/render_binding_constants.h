#pragma once

// engine/assets/render_binding_layout/render_binding_constants.h
//
// The root-constant vocabulary shared by authored render binding layouts
// (issue #227) and their consumers. Split out of render_binding_layout.h as a
// dependency-free leaf so RenderProgramData (render_program.h) and
// RhiRenderableRecipe (renderable.h) can carry the layout's constants contract
// without the render_binding_layout.h → render_program.h → renderable.h
// include chain turning circular (issue #228).

#include <cstdint>
#include <string>

namespace wz::engine::assets
{
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

    // Where a field starting no earlier than `running_dwords` actually lands
    // under HLSL cbuffer packing: a vector may not straddle a 16-byte register
    // boundary, so a 2/3/4-dword field that would cross one starts at the next
    // register instead (scalars pack tightly). The shader reads the block as a
    // cbuffer, so THIS rule — not tight packing — is the byte contract every
    // consumer must share: the recipe's baked field offsets
    // (renderable_compilers.cpp), the block-size derivation (tail_dwords), and
    // the generated HLSL binding prelude's packoffset emission (issue #231).
    [[nodiscard]] constexpr uint32_t render_binding_constant_field_offset(
        uint32_t running_dwords, RenderBindingConstantType type) noexcept
    {
        const uint32_t dwords = render_binding_constant_type_dwords(type);
        const uint32_t lane = running_dwords % 4u;
        if (dwords > 1u && lane + dwords > 4u) {
            return running_dwords + (4u - lane);
        }
        return running_dwords;
    }

    // Tail packing computes offsets relative to the head's end but must equal
    // the absolute in-block offsets; that holds only because every head packer
    // ends on a 16-byte register boundary.
    static_assert(
        render_binding_constants_head_dwords(
            RenderBindingConstantsHead::None) % 4u == 0u
        && render_binding_constants_head_dwords(
            RenderBindingConstantsHead::Mvp16) % 4u == 0u
        && render_binding_constants_head_dwords(
            RenderBindingConstantsHead::WorldViewProjCamera36) % 4u == 0u
        && render_binding_constants_head_dwords(
            RenderBindingConstantsHead::Clipmap32) % 4u == 0u,
        "head packers must end register-aligned for relative tail offsets");
}
