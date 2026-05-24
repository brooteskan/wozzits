#pragma once

// engine/assets/gaussian_splat/gaussian_splat_terrain_recipe.h
//
// Recipe types for multi-field terrain splat compilation.
//
// A TerrainSplatFieldRecipe describes how a ScalarFieldSet (named
// co-registered scalar fields) maps to a GaussianSplatCloud.  The
// recipe separates geometry (driven by a height field) from color
// (driven by arbitrary named fields via FieldMap entries) and from
// optional geometry-detail fields (normal, curvature, peaks).
//
// Color modes:
//   SlopeHeightDebug  - existing slope-modulated grayscale fallback
//   RgbFields         - three named fields mapped to R, G, B
//   GrayscaleField    - one named field replicated into R=G=B
//
// Normal sources:
//   FiniteDifference  - central-difference normal from heightfield
//   ImportedField     - three named scalar fields (x, y, z) decoded
//   Blend             - lerp between FD and imported
//
// Curvature/Peaks: optional fields that modify radius, opacity, color.
//
// FieldMap applies: normalize -> scale -> bias -> clamp to [0,1].
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

    // ── Normal source ──────────────────────────────────────────────────

    enum class NormalSource : uint8_t
    {
        FiniteDifference,   // central-difference from heightfield (existing)
        ImportedField,      // three scalar fields decoded as (x, y, z)
        Blend,              // lerp between FD and imported
    };

    // How the normal field channels are encoded in [0,1] or [-1,1] range.
    enum class NormalEncodedRange : uint8_t
    {
        MinusOneToOne,  // raw values are already in [-1, 1]
        ZeroToOne,      // raw values are in [0, 1]; decode: v * 2 - 1
    };

    // Which axis in the source normal data points "up."
    enum class NormalUpAxis : uint8_t
    {
        Y,   // Wozzits convention: +Y is up
        Z,   // Common in Gaea/Blender: +Z is up -> swap Y<->Z
    };

    // Normal debug visualization modes.
    enum class NormalDebugView : uint8_t
    {
        Off,                 // normal recipe color
        FiniteDifference,    // FD normal -> RGB
        Imported,            // imported normal -> RGB
        Blended,             // final blended normal -> RGB
        Difference,          // abs(FD - imported) -> RGB
    };

    // Normal field configuration.
    struct NormalFieldConfig
    {
        NormalSource       source        = NormalSource::FiniteDifference;

        // Field names in ScalarFieldSet for imported normals.
        std::string        field_x       = "normal_x";
        std::string        field_y       = "normal_y";
        std::string        field_z       = "normal_z";

        NormalEncodedRange encoded_range = NormalEncodedRange::MinusOneToOne;
        NormalUpAxis       up_axis       = NormalUpAxis::Y;
        bool               flip_x       = false;
        bool               flip_z       = false;

        // Blend factor when source == Blend.
        // 0.0 = pure finite-difference, 1.0 = pure imported.
        float              blend         = 1.0f;
    };

    // ── Curvature field configuration ──────────────────────────────────
    //
    // Optional scalar field that modifies splat radius and opacity at
    // high-curvature regions (ridges, creases, sharp terrain changes).
    //
    // High curvature -> tighter splats, slightly higher opacity.
    // Low curvature  -> broader, smoother footprint (default behavior).
    //
    // When field_name is empty, curvature has no effect.

    struct CurvatureFieldConfig
    {
        std::string field_name;       // empty = disabled
        bool        normalize = true;
        float       scale     = 1.0f;
        float       bias      = 0.0f;

        // Influence on tangent-plane radius.
        // 0 = no effect, 1 = full curvature-based radius reduction.
        float radius_influence  = 0.0f;
        // At mapped_curvature=1, radius is multiplied by this.
        float min_radius_mul    = 0.5f;

        // Influence on opacity.
        // 0 = no effect.
        float opacity_influence = 0.0f;
        // At mapped_curvature=1, opacity is multiplied by this.
        float opacity_boost     = 1.25f;
    };

    // ── Peaks field configuration ──────────────────────────────────────
    //
    // Optional scalar field marking local peaks/ridges.
    // Modifies color brightness, opacity, and/or radius to preserve
    // feature visibility under broad smoothing.
    //
    // When field_name is empty, peaks have no effect.

    struct PeaksFieldConfig
    {
        std::string field_name;       // empty = disabled
        bool        normalize = true;
        float       scale     = 1.0f;
        float       bias      = 0.0f;

        // Color brightness boost at mapped_peaks=1.
        float color_influence         = 0.0f;
        float color_brightness_boost  = 0.3f;

        // Opacity boost at mapped_peaks=1.
        float opacity_influence       = 0.0f;
        float opacity_boost           = 1.15f;

        // Radius reduction at mapped_peaks=1.
        float radius_influence        = 0.0f;
        float min_radius_mul          = 0.8f;
    };

    // ── Recipe ─────────────────────────────────────────────────────────
    //
    // Captures the full specification for compiling a terrain splat cloud
    // from a ScalarFieldSet.  Geometry comes from the named height field;
    // color comes from the color mode + field maps; orientation, scale,
    // and opacity can be refined by optional geometry-detail fields.

    struct TerrainSplatFieldRecipe
    {
        // ── Geometry ───────────────────────────────────────────────────
        // Name of the height field in the ScalarFieldSet.
        std::string height_field_name = "height";

        // Compile parameters for the terrain surface geometry.
        GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc geometry;

        // ── Color ──────────────────────────────────────────────────────
        TerrainSplatColorMode color_mode = TerrainSplatColorMode::SlopeHeightDebug;

        // Used when color_mode == RgbFields.
        FieldMap color_r;
        FieldMap color_g;
        FieldMap color_b;

        // Used when color_mode == GrayscaleField.
        FieldMap color_gray;

        // ── Normal ─────────────────────────────────────────────────────
        NormalFieldConfig normal;

        // ── Curvature ──────────────────────────────────────────────────
        CurvatureFieldConfig curvature;

        // ── Peaks ──────────────────────────────────────────────────────
        PeaksFieldConfig peaks;

        // ── Debug ──────────────────────────────────────────────────────
        NormalDebugView normal_debug = NormalDebugView::Off;
    };

    // ── Validation result ──────────────────────────────────────────────

    struct TerrainSplatRecipeValidation
    {
        bool ok = false;
        std::string error;
    };

}  // namespace wz::engine::assets
