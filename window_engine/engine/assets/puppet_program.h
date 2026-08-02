#pragma once

// engine/assets/puppet_program.h
//
// Engine-provided render program for Inochi2D puppets (inochi S2c). A puppet is
// a SPECIAL CASE: unlike a mesh renderable, its rendering is DEFINED by the
// format (it should appear as it does in the Inochi editor), so the program is
// never authored by the user. The engine owns the one canonical puppet program
// and provisions it on demand — the puppet renderable only needs a Puppet.
//
// This mirrors ensure_mesh_style_pull_program: the file carrier resolves shader
// paths against the PROJECT root (no engine-resource fallback), so the canonical
// HLSL source is embedded here and staged into <project>/shaders/puppet/ on
// demand, OVERWRITING a staged copy whose content differs (#283 -- a stale
// staged shader used to render silently wrong). There is deliberately NO copy
// under the engine's
// resources/shaders/ — this file is the single source of truth for the puppet
// shaders + their SRG (the SRG here MUST match the shaders, and both match the
// per-Part packet build in rhi_scene_renderer.cpp).
//
// GPU gating: shader assets cannot compile without a device, so callers must
// gate provisioning on device validity — deviceless, a puppet simply gets no
// renderable, matching the app model (and the mesh_style / mesh_vertex_pull
// device gates).
//
// The puppet program comes as a fixed SET of blend VARIANTS (#274) — identical
// shaders and SRG, differing only in the PSO's blend state, one per
// fixed-function blend mode a Part can author: Normal, Multiply, Screen (#274)
// and the two lower-relative modes, ClipToLower / SliceFromLower (#299). Blend
// state lives in the PSO and DrawRequest carries a single program tag, so
// per-Part blend means per-Part programs; the renderer picks one per Part from
// ResidentPuppetPart::blend. The set is ENGINE-FIXED, not authored: the variants
// are provisioned together (typed path and graph path alike) and found by
// puppet_program_variants(), so nothing in the scene or the editor has to wire
// them. The destination-COLOUR modes (Overlay, SoftLight, ColorBurn, ...) have
// no fixed-function form and fall back to Normal until a backdrop-sampling seam
// lands.

#include <engine/assets/inochi/inochi_puppet.h>  // inochi::BlendMode
#include <engine/assets/render_program/render_program.h>
#include <engine/assets/render_program/render_program_asset_module.h>
#include <engine/assets/shader_asset_module.h>
#include <engine/assets/file_carrier_asset_module.h>

#include <asset/system.h>
#include <logging/logger.h>

#include <file/filesystem.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace wz::engine::assets
{
    // The blend variants of the puppet program (#274). Values are STABLE: they
    // are the kPuppetProgramSchema "blend_mode" enum param, so they participate
    // in each variant node's content-addressed key — reordering them would
    // repoint existing graphs at the wrong program.
    enum class PuppetProgramBlend : std::uint8_t
    {
        Normal = 0,    // premultiplied "over" — also the fallback for any
                       // Part mode without a variant (masks, Overlay, ...)
        Multiply = 1,
        Screen = 2,
        // Inochi's two LOWER-relative modes (#299). They clip against what has
        // already been drawn rather than against a named mask source, which
        // reads like it needs a backdrop sample -- but both are destination
        // ALPHA operations, and those a PSO expresses directly: ClipToLower is
        // Porter-Duff source-atop, SliceFromLower is its cutting counterpart.
        // Only the modes that need destination COLOUR (Overlay, SoftLight,
        // ColorBurn, ...) are genuinely shader-emulated work.
        ClipToLower = 3,
        SliceFromLower = 4,
        // Inochi's AddGlow / LinearDodge. Pure src*ONE + dst*ONE: no backdrop
        // sample needed, so a PSO expresses it directly and rhi::BlendMode
        // already has the member. Both used to fall into the `default:` of
        // puppet_program_blend_for_part alongside the modes that genuinely need
        // destination COLOUR, and drew as plain "over" -- a glow layer
        // rendering flat (issue #316, C3-C6). APPENDED, never reordered: these
        // ordinals are baked into variant content keys.
        Additive = 5,
    };
    inline constexpr std::size_t kPuppetProgramBlendCount = 6;

    // The kPuppetProgramSchema parameter carrying PuppetProgramBlend. The
    // compiler MUST declare it in .parameters or authored nodes never get a
    // meta ParamBlock at all and every variant would collapse to Normal.
    inline constexpr const char* kPuppetProgramBlendParam = "blend_mode";

    // Distinct asset names per variant. The blend mode is already mixed into
    // create_custom's key, so this is for legibility (it becomes the rhi
    // program label) rather than uniqueness. Normal keeps the original name so
    // its key is unaffected by the variant set arriving.
    [[nodiscard]] const char* puppet_program_name(PuppetProgramBlend blend);

    // The rhi blend state a variant compiles to. Normal is PremultipliedAlpha
    // because the puppet's atlas + pixel shader work in premultiplied space
    // (#277); Multiply/Screen are the coverage-aware rhi recipes; ClipToLower /
    // SliceFromLower are SourceAtop / SliceFromDestination (#299).
    //
    // Defined here rather than in the .cpp so the injectivity static_assert
    // below can evaluate it: this mapping MUST be one-to-one, and that is not a
    // stylistic preference. puppet_program_variants_from_graph recovers which
    // variant a compiled program is by searching for the ordinal whose
    // rhi_blend_for matches the program's COMPILED blend state, taking the
    // first match. Two ordinals sharing an rhi mode would silently file one
    // variant's key under the other and leave the loser's slot empty --
    // falling back to Normal, i.e. drawing the wrong blend with no diagnostic.
    [[nodiscard]] constexpr wz::rhi::BlendMode rhi_blend_for(
        PuppetProgramBlend blend) noexcept
    {
        switch (blend) {
        case PuppetProgramBlend::Multiply: return wz::rhi::BlendMode::Multiply;
        case PuppetProgramBlend::Screen:   return wz::rhi::BlendMode::Screen;
        case PuppetProgramBlend::ClipToLower:
            return wz::rhi::BlendMode::SourceAtop;
        case PuppetProgramBlend::SliceFromLower:
            return wz::rhi::BlendMode::SliceFromDestination;
        case PuppetProgramBlend::Additive: return wz::rhi::BlendMode::Additive;
        case PuppetProgramBlend::Normal:   break;
        }
        return wz::rhi::BlendMode::PremultipliedAlpha;
    }

    // The reverse lookup's precondition, checked by the compiler instead of
    // trusted. Appending a variant that reuses an existing rhi blend mode now
    // fails the build here rather than mis-rendering at runtime.
    [[nodiscard]] constexpr bool puppet_program_blends_map_injectively() noexcept
    {
        for (std::size_t i = 0; i < kPuppetProgramBlendCount; ++i) {
            for (std::size_t j = i + 1; j < kPuppetProgramBlendCount; ++j) {
                if (rhi_blend_for(static_cast<PuppetProgramBlend>(i))
                    == rhi_blend_for(static_cast<PuppetProgramBlend>(j)))
                {
                    return false;
                }
            }
        }
        return true;
    }

    static_assert(
        puppet_program_blends_map_injectively(),
        "two PuppetProgramBlend variants map to the same wz::rhi::BlendMode; "
        "puppet_program_variants_from_graph recovers a variant by matching its "
        "compiled blend state and takes the first match, so one of the two "
        "would be filed under the other and the loser would silently render as "
        "Normal. Give the new variant its own rhi::BlendMode, or drop it.");

    // The variant a Part's authored Inochi blend mode draws through. What is
    // left without one is the modes that read destination COLOUR (Overlay,
    // SoftLight, ColorBurn, Difference, ...); those map to Normal until a
    // backdrop-sampling seam lands, which is how they render today.
    [[nodiscard]] PuppetProgramBlend puppet_program_blend_for_part(
        inochi::BlendMode part_blend);

    // The engine-provisioned puppet programs, indexed by PuppetProgramBlend.
    // An entry is empty when that variant is absent (a graph authored before
    // #274 carries only Normal); callers fall back to Normal.
    struct PuppetProgramVariants
    {
        std::array<wz::asset::AssetKey, kPuppetProgramBlendCount> by_blend{};

        [[nodiscard]] const wz::asset::AssetKey& key_for(
            PuppetProgramBlend blend) const noexcept
        {
            const std::size_t i = static_cast<std::size_t>(blend);
            const wz::asset::AssetKey& k =
                i < by_blend.size() ? by_blend[i] : by_blend[0];
            return k == wz::asset::AssetKey{} ? by_blend[0] : k;
        }
    };

    // Find the blend-variant siblings of an already-resolved puppet program.
    // `base` is the program the puppet renderable binds (any variant); the set
    // is every registered RENDER PROGRAM sharing base's SHADER DEPS, bucketed by
    // the blend state it COMPILED TO. Matching on the shader deps is what keeps
    // two different puppet programs in one project apart.
    //
    // Neither half of that is incidental (#300). Matching a SCHEMA instead found
    // nothing for programs provisioned the typed way -- ensure_puppet_program
    // registers kCustomRenderProgramSchema, the graph path registers
    // kPuppetProgramSchema -- so every Part silently drew Normal. And bucketing
    // by the COMPILED blend rather than by an authored param means this cannot
    // disagree with the PSO that will actually draw, and reads the same however
    // the program was authored.
    //
    // Returns a set whose Normal slot is `base` itself when no siblings exist,
    // so the caller always has something to draw with.
    [[nodiscard]] PuppetProgramVariants puppet_program_variants(
        const wz::asset::AssetSystem& system,
        const RenderProgramAssetModule& programs,
        const wz::asset::AssetKey& base);

    // Stages the embedded puppet shader sources into <project>/shaders/puppet/
    // (overwriting a stale copy, #283), registers the shader pair, and creates/dedups the
    // custom render programs carrying the fixed puppet SRG (Screen view head at
    // t0/space0; PuppetVertices t0 / PuppetIndices t1 / PuppetAtlas t2 + a
    // LinearClamp sampler s0 in space2; the 16-dword "puppet_part" root-constant
    // block at b0/space2) — ONE PER BLEND VARIANT. Idempotent: a second call
    // returns the same assets. `out_variants` receives every variant's key.
    // Returns the Normal variant, or an invalid asset on failure (caller
    // logs/skips the renderable).
    RenderProgramAsset ensure_puppet_program(
        wz::Logger& logger,
        FileCarrierAssetModule& files,
        ShaderAssetModule& shaders,
        RenderProgramAssetModule& render_programs,
        PuppetProgramVariants* out_variants = nullptr);

    // The fixed puppet SRG as a CustomRenderProgramDesc for one blend variant
    // (name set; shaders UNSET — the caller assigns vertex_shader/pixel_shader).
    // Shared by ensure_puppet_program (typed path) and the kPuppetProgramSchema
    // compiler (graph path), so the SRG lives in exactly one place.
    [[nodiscard]] CustomRenderProgramDesc puppet_program_srg_desc(
        const std::string& name,
        PuppetProgramBlend blend = PuppetProgramBlend::Normal);

    // Project-root-relative paths of the canonical puppet shaders. Single source
    // of truth for both the typed ensure_puppet_program() path and the graph-
    // authoring routine (which references them as ShaderSource nodes).
    inline constexpr const char* kPuppetVertexShaderProjectPath =
        "shaders/puppet/puppet_vs.hlsl";
    inline constexpr const char* kPuppetPixelShaderProjectPath =
        "shaders/puppet/puppet_ps.hlsl";

    // Stage the embedded puppet shader sources into <project>/shaders/puppet/,
    // resolving the project-relative paths via resolve_path. A staged file whose
    // content differs from the embedded source is OVERWRITTEN (#283); one that
    // matches is left untouched, so an unchanged project sees no write at all.
    // A hand-edit of a staged shader therefore does NOT survive the next run --
    // iterate on the embedded source instead.
    // Returns false on an IO failure (directory create / write). Shared by the
    // typed ensure_puppet_program() path (FileCarrierAssetModule::resolve_path)
    // and the graph-authoring routine (GraphAuthoringContext::resolve_file), so
    // the embedded sources live in exactly one place.
    [[nodiscard]] bool stage_puppet_shaders(
        wz::Logger& logger,
        const std::function<wz::fs::Path(const wz::fs::Path&)>& resolve_path);
}
