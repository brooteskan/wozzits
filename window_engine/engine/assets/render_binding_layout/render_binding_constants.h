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
}
