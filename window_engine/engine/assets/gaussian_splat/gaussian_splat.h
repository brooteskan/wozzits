#pragma once

// engine/assets/gaussian_splat/gaussian_splat.h

#include <engine/assets/gaussian_splat/gaussian_splat_cloud.h>

#include <asset/types.h>

#include <cstdint>
#include <vector>
#include <string>

namespace wz::engine::assets
{
    class GaussianSplatCloudTable
    {
    public:
        GaussianSplatCloudTable();

        wz::asset::ResourceHandle add(GaussianSplatCloudData cloud);
        const GaussianSplatCloudData* get(wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        std::vector<GaussianSplatCloudData> clouds_;
        std::vector<uint32_t> epochs_;
    };

    struct ProceduralGaussianSplatCloudDesc
    {
        std::string name;

        uint32_t count = 0;
        float radius = 1.0f;
        float splat_scale = 0.05f;
    };

    struct ProceduralGaussianSplatCloudCompileDesc
    {
        uint32_t count = 0;
        float radius = 1.0f;
        float splat_scale = 0.05f;
    };

    struct GaussianSplatFromScalarFieldCompileDesc
    {
        float height_scale = 1.0f;    // multiplier applied to field value → world Y
        float step_x = 1.0f;         // world-space X distance between grid columns
        float step_z = 1.0f;         // world-space Z distance between grid rows
        float splat_scale = 0.05f;
        float opacity = 0.9f;
        bool normalize_values = true; // normalize value into [0,1] before use
        bool use_threshold = false;   // skip splats whose normalized value < emit_threshold
        float emit_threshold = 0.0f;
    };


    // ─── Terrain surface splat compiler descriptor ──────────────────────────
    //
    // Distinct from GaussianSplatFromScalarFieldCompileDesc (a simple grayscale
    // heightmap debug splatter):
    //
    //   • Heightmap values are treated as RAW WORLD ELEVATIONS multiplied by
    //     height_scale as an exaggeration factor (no [0,1] normalization).
    //   • Per-splat orientation is the surface tangent frame, not identity.
    //   • Per-splat scale is anisotropic — tangent-plane sizes come from the
    //     local heightmap tangent magnitudes |Tu|, |Tv|, so splats stretch
    //     along the surface as slope grows.  Normal-axis scale is a fixed
    //     thickness so splats look like little discs, not spheres.
    //   • Color is slope-modulated grayscale.
    //
    // Subsampling: if subsample_step > 1, the compiler emits every Nth cell
    // in both u and v and grows the tangent extents by N so the surface
    // remains continuous instead of stippling.

    struct GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc
    {
        float height_scale    = 1.0f;  // exaggeration multiplier on raw field values
        float step_x          = 1.0f;  // world-space X distance between grid columns
        float step_z          = 1.0f;  // world-space Z distance between grid rows

        // Splat tangent-plane extent multiplier.  ~1.0 = cells exactly meet;
        // 1.2–1.5 = neighbours overlap, surface looks continuous.
        float overlap_factor  = 1.25f;

        // Splat extent along the surface normal axis (world units).
        // Auto = 0 (default) → 0.05 * max(step_x, step_z) at compile time.
        // Set to a positive value to override.
        float thickness       = 0.0f;

        // Emit every Nth cell in both u and v.  >= 1.  Tangent extents are
        // multiplied by this factor to keep coverage continuous.
        uint32_t subsample_step = 1;

        // Splat opacity (0..1).  Encoded as logit on output.
        float opacity         = 0.95f;

        // Color: slope-modulated grayscale.  display(slope=0)=flat_luminance,
        // display(slope=1)=steep_luminance.
        float flat_luminance  = 0.55f;
        float steep_luminance = 0.30f;

        // ── Slope-stretch clamp ──
        //
        // Per-splat tangent-plane scale comes from the heightfield tangent
        // magnitude |Tu|, |Tv|.  On steep slopes |Tu| >> step_x because
        // the height component dominates.  This is geometrically correct
        // (the adjacent cell IS further away along the slope) but for
        // terrain rendering it produces extremely elongated discs at
        // borders, creases, and any feature with a steep local gradient.
        //
        // max_slope_stretch clamps |Tu| to step_x * max_slope_stretch
        // (and likewise for |Tv| / step_z).  The rotation quaternion
        // already aligns the disc to the surface, so clamped scales still
        // produce a correctly-oriented disc — it just doesn't extend as
        // far along extreme slopes.
        //
        //   1.0 = grid-only (all discs same size, no slope stretch)
        //   2.0 = moderate; covers slopes up to ~63°
        //   4.0 = generous; covers slopes up to ~76°
        //  10.0 = nearly unlimited
        float max_slope_stretch = 2.0f;

        // ── Normal smoothing ──
        //
        // When enabled, the per-cell raw finite-difference normals are
        // computed over the FULL source grid, then each emitted splat's
        // normal is the Gaussian-weighted average of raw normals in a
        // neighbourhood of `normal_smoothing_radius_cells` cells around
        // the splat's source position.  The smoothed normal then drives
        // the splat's tangent frame; position and tangent-extent scale are
        // unchanged.
        //
        // Effect: smooths discontinuities in the normal field caused by
        // local heightmap noise.  At grazing camera angles, adjacent
        // patches read as a continuous tangent field instead of showing
        // visible slits/steps between independent local tangent planes.
        //
        // This is NOT the same as inflating splat radius — patches stay
        // the same physical size.  Only their orientation is smoothed.
        bool     normal_smoothing_enabled       = false;
        uint32_t normal_smoothing_radius_cells  = 2;
        float    normal_smoothing_sigma_cells   = 1.0f;
    };
}