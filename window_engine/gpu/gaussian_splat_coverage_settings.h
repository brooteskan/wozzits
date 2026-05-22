#pragma once

// gpu/gaussian_splat_coverage_settings.h
//
// Scene-wide settings consumed by the GaussianSplatTerrainCoverageDebug
// render program.  Sibling to gaussian_splat_color_lod_settings.h.
//
// These settings select between the four coverage modes the program supports
// and tune the threshold / opacity controls.  Coverage is a render-policy
// experiment: the splat geometry path is unchanged but the pixel shader
// produces either a transparent blend (the default 3DGS behaviour) or a
// thresholded coverage signal that participates in the depth buffer.

#include <gpu/gpu.h>

#include <cstdint>

namespace wz::gpu
{
    enum class SplatCoverageMode : uint32_t
    {
        // Reference / debug: current 3DGS-style soft Gaussian alpha blend.
        // Identical visual result to the pull-debug shader's PS.
        TransparentBlend = 0,

        // Hard cutoff: discard pixels whose Gaussian coverage is below
        // `threshold`; opaque inside the kept region.  Writes depth.
        // Useful for terrain — produces an occlusion signal while keeping
        // the per-splat oriented-disc geometry.
        CoverageDiscard = 1,

        // Stable per-pixel hash compared against Gaussian coverage to
        // decide kept-vs-discarded.  Splats remain visually soft (dithered
        // edges) but still write depth.  The hash must be stable across
        // frames so the dither does not shimmer.
        DitheredCoverage = 2,

        // Hard unit-disc: discard outside r=1 in splat uv space, opaque
        // inside.  Coarsest baseline — useful for visually verifying the
        // disc geometry before tuning Gaussian thresholds.
        OpaqueDisc = 3,
    };

    // Kernel: maps normalized splat-local radius r ∈ [0, ~quad_extent] to a
    // coverage value in [0, 1].  Mode selects the shape; mode parameters
    // (inner_radius / outer_radius / gaussian_falloff) tune it.  The
    // coverage value then feeds whichever SplatCoverageMode is active —
    // CoverageDiscard cuts at `threshold`, DitheredCoverage compares to a
    // stable hash, TransparentBlend uses it as alpha.
    enum class TerrainCoverageKernelMode : uint32_t
    {
        // Gaussian falloff: coverage = exp(-0.5 * r² * gaussian_falloff).
        // Reference behaviour matching the original 3DGS PS.
        Gaussian = 0,

        // Smoothstep disc: coverage = 1 - smoothstep(inner_radius, outer_radius, r).
        // Full interior, controlled soft edge.  Best default for terrain —
        // gives connected surface coverage between neighbouring splats
        // instead of the "isolated dots" look the Gaussian produces close-up.
        SmoothDisc = 1,

        // Polynomial disc: coverage = saturate(1 - r_norm²)².
        // Cheap, smooth, no sharp edge.  Similar to SmoothDisc with
        // inner_radius=0 — full interior with quadratic falloff.
        PolynomialDisc = 2,

        // Hard unit disc: coverage = 1 if r_norm ≤ 1 else 0.
        // Baseline / debug — confirms disc geometry without kernel tuning.
        HardDisc = 3,
    };

    // Optional debug visualisation overrides for the coverage PS.  When
    // non-Off, the PS replaces its colour output with the chosen
    // visualisation so we can verify patch orientation and footprint.
    enum class TerrainCoverageDebugView : uint32_t
    {
        Off          = 0,  // normal colour output
        PatchUV      = 1,  // uv.x → R, uv.y → G, 0.5 → B
        Normal       = 2,  // world-space patch normal → RGB
        TangentSign  = 3,  // sign(tangent_u·worldX)/sign(tangent_v·worldZ) tint
        Coverage     = 4,  // kernel coverage as grayscale
    };

    struct SplatCoverageSettings
    {
        SplatCoverageMode mode = SplatCoverageMode::TransparentBlend;

        // Coverage threshold in [0,1].  Pixels whose kernel coverage is
        // below this are discarded by CoverageDiscard; serves as the median
        // for DitheredCoverage.
        float threshold = 0.5f;

        // Multiplier applied to per-splat opacity in TransparentBlend mode.
        // The default 3DGS PS used a hardcoded 0.25 readability scale; this
        // exposes it so terrain can dial it up without modifying the
        // existing pull-debug program.
        float opacity_scale = 0.25f;

        // ── Footprint / kernel controls ──

        // Which kernel evaluates per-pixel coverage from the splat-local
        // normalized radius.
        TerrainCoverageKernelMode kernel_mode =
            TerrainCoverageKernelMode::SmoothDisc;

        // Multiplier on the projected splat axes.  >1 grows the footprint
        // (more overlap with neighbours); <1 shrinks it.  Use this to make
        // terrain splats visually connect into a continuous surface instead
        // of reading as isolated discs.
        float radius_scale = 1.0f;

        // SmoothDisc inner edge (full coverage inside) and outer edge
        // (coverage = 0 outside).  Normalized [0, 1] with 1 at the quad
        // support boundary.
        float inner_radius = 0.65f;
        float outer_radius = 1.0f;

        // Gaussian kernel falloff parameter.  Used by the PS as
        //   coverage = exp(-r_norm² * gaussian_falloff)
        // where r_norm ∈ [0, 1] at the tangent-patch axis edge.  Default 4.0
        // gives coverage ≈ 0.018 at the edge — a reasonable Gaussian shape
        // that still fades to nearly zero at the patch boundary.
        float gaussian_falloff = 4.0f;

        // Minimum projected splat radius in pixels.  Plumbed through the
        // pipeline but currently unused by the tangent-plane VS (which
        // expands the patch in world space, not in screen space).  Reserved
        // for a future screen-space clamp step.
        float min_screen_radius_px = 0.0f;

        // Optional debug visualisation override.
        TerrainCoverageDebugView debug_view = TerrainCoverageDebugView::Off;
    };

    // Push scene-wide coverage settings onto the device.  Call once per
    // frame from the toolhost before any draws.  Persists until next call.
    void set_splat_coverage_settings(
        Device& device,
        const SplatCoverageSettings& settings);
}
