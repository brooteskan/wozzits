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

    struct SplatCoverageSettings
    {
        SplatCoverageMode mode = SplatCoverageMode::TransparentBlend;

        // Coverage threshold in [0,1].  Pixels with Gaussian coverage below
        // this value are discarded by CoverageDiscard; serves as the median
        // for DitheredCoverage.
        float threshold = 0.5f;

        // Multiplier applied to per-splat opacity in TransparentBlend mode.
        // The default 3DGS PS used a hardcoded 0.25 readability scale; this
        // exposes it so terrain can dial it up without modifying the
        // existing pull-debug program.
        float opacity_scale = 0.25f;
    };

    // Push scene-wide coverage settings onto the device.  Call once per
    // frame from the toolhost before any draws.  Persists until next call.
    void set_splat_coverage_settings(
        Device& device,
        const SplatCoverageSettings& settings);
}
