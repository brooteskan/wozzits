#pragma once

// engine/assets/gaussian_splat/gaussian_splat_terrain_recipe.h
//
// Recipe types for multi-field terrain splat compilation.
//
// A TerrainSplatFieldRecipe describes how a ScalarFieldSet (named
// co-registered scalar fields) maps to a GaussianSplatCloud.  The
// recipe separates geometry (driven by a height field) from color
// (driven by arbitrary named fields via FieldMap entries).
//
// Color modes:
//   SlopeHeightDebug  - existing slope-modulated grayscale fallback
//   RgbFields         - three named fields mapped to R, G, B
//   GrayscaleField    - one named field replicated into R=G=B
//
// FieldMap applies: normalize → scale → bias → clamp to [0,1].
// This is generic routing, not semantic interpretation.

#include <engine/assets/gaussian_splat/gaussian_splat.h>

#include <cstdint>
#include <string>

namespace wz::engine::assets
{
    // ── Field-to-attribute mapping ─────────────────────────────────────
    //
    // Maps a named scalar field value to a [0,1] attribute value.
    //
    // Pipeline:
    //   raw = field.at(ix, iz)
    //   if normalize:  raw = (raw - field.min) / max(field.max - field.min, eps)
    //   mapped = raw * scale + bias
    //   result = clamp(mapped, 0, 1)

    struct FieldMap
    {
        std::string field_name;        // key into ScalarFieldSet
        bool        normalize = true;  // normalize raw value to [0,1] using field min/max
        float       scale     = 1.0f;
        float       bias      = 0.0f;
    };

    // ── Color mode ─────────────────────────────────────────────────────

    enum class TerrainSplatColorMode : uint8_t
    {
        // Existing slope-modulated grayscale.  Uses flat_luminance /
        // steep_luminance from the compile desc.  No field mapping.
        SlopeHeightDebug,

        // Three named fields mapped independently to R, G, B.
        RgbFields,

        // One named field replicated into R = G = B.
        GrayscaleField,
    };

    // ── Recipe ─────────────────────────────────────────────────────────
    //
    // Captures the full specification for compiling a terrain splat cloud
    // from a ScalarFieldSet.  Geometry comes from the named height field;
    // color comes from the color mode + field maps.
    //
    // The geometry compile desc (overlap, thickness, smoothing, etc.) is
    // stored in the existing GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc.
    // This recipe struct adds the multi-field color routing on top.

    struct TerrainSplatFieldRecipe
    {
        // ── Geometry ───────────────────────────────────────────────────
        // Name of the height field in the ScalarFieldSet.
        std::string height_field_name = "height";

        // Compile parameters for the terrain surface geometry.
        // height_scale, step_x, step_z are data-dependent and set per-field.
        // overlap_factor, thickness, smoothing, etc. come from preset or UI.
        GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc geometry;

        // ── Color ──────────────────────────────────────────────────────
        TerrainSplatColorMode color_mode = TerrainSplatColorMode::SlopeHeightDebug;

        // Used when color_mode == RgbFields.
        FieldMap color_r;
        FieldMap color_g;
        FieldMap color_b;

        // Used when color_mode == GrayscaleField.
        FieldMap color_gray;
    };

    // ── Validation result ──────────────────────────────────────────────

    struct TerrainSplatRecipeValidation
    {
        bool ok = false;
        std::string error;
    };

}  // namespace wz::engine::assets
