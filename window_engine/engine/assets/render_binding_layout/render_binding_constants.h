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
    // world[16]+view_proj[16]+camera[4], camera_snapped_terrain →
    // ClipmapDrawConstants: view_projection[16] + a 16-dword camera-follow
    // terrain block the renderer computes each frame via clipmap_view.h).
    enum class RenderBindingConstantsHead : uint8_t
    {
        None,
        Mvp16,
        WorldViewProjCamera36,
        // Camera-follow displaced-terrain head (issue #233): the clipmap's
        // 32-dword block generalized — view_projection + per-level snap /
        // world→uv / vertical / texel-extent params. The renderer fills it from
        // the live camera + the recipe's ClipmapLandscapeRenderSettings + the
        // bound height field's dims + the lattice geometry (make_clipmap_draw_
        // constants). Any camera-snapped terrain shader can use it; the clipmap
        // is one consumer. (Was named Clipmap32 while it lived only on the
        // bespoke 0x708 path.)
        CameraSnappedTerrain,
    };

    [[nodiscard]] constexpr uint32_t render_binding_constants_head_dwords(
        RenderBindingConstantsHead head) noexcept
    {
        switch (head) {
        case RenderBindingConstantsHead::None:                  return 0u;
        case RenderBindingConstantsHead::Mvp16:                 return 16u;
        case RenderBindingConstantsHead::WorldViewProjCamera36: return 36u;
        case RenderBindingConstantsHead::CameraSnappedTerrain:  return 32u;
        }
        return 0u;
    }

    // Head of the VIEW-frequency root-constant block (register space 0).
    //
    // Everything above describes what one DRAW needs. This describes what the
    // FRAME needs -- state that is a property of the world and the observer
    // rather than of any renderable, so every program should see the same
    // values and none should carry its own copy.
    //
    // It exists because the alternative is worse. Fog needs the camera
    // position, and today each recipe carries the camera inside its own head at
    // its own offset (snap_params.xy for camera-snapped terrain,
    // camera_and_diameter for splat clouds), so a shared helper would have to
    // know which head it was compiled against. Hoisting the observer to its own
    // frequency is what makes a helper genuinely uniform -- and it is the same
    // place scene lighting belongs, which the clipmap pixel shader currently
    // hardcodes for want of anywhere to put it.
    //
    // OPTIONAL: a layout that declares None emits no space-0 registers and its
    // derived SRG is byte-identical to one authored before this existed.
    enum class RenderBindingViewHead : uint8_t
    {
        None,
        // Observer + atmosphere. 12 dwords:
        //   c0  float4  camera_world_position.xyz, unused
        //   c1  float4  fog_color.rgb, fog_density
        //   c2  float4  fog_start_distance, fog_height_falloff, enabled, unused
        // Filled once per frame from the camera and the Atmosphere asset.
        CameraFog,
        // Viewport size for screen-space passes (2D overlays / HUD). 4 dwords:
        //   c0  float4  width, height, 1/width, 1/height
        // Filled once per frame from the render target's dimensions, so a VS can
        // map a pixel rect to NDC. A layout picks EITHER CameraFog OR Screen -- an
        // overlay wants pixels, not fog -- each bound to its own per-frame buffer
        // under its own descriptor semantic.
        Screen,
    };

    [[nodiscard]] constexpr uint32_t render_binding_view_head_dwords(
        RenderBindingViewHead head) noexcept
    {
        switch (head) {
        case RenderBindingViewHead::None:      return 0u;
        case RenderBindingViewHead::CameraFog: return 12u;
        case RenderBindingViewHead::Screen:    return 4u;
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
            RenderBindingConstantsHead::CameraSnappedTerrain) % 4u == 0u,
        "head packers must end register-aligned for relative tail offsets");
}
