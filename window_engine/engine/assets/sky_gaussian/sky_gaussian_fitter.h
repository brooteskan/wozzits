#pragma once

// engine/assets/sky_gaussian/sky_gaussian_fitter.h
//
// Deterministic, pure-CPU fit of a SkyGaussianSet to an equirectangular HDR
// panorama. No random-number generation anywhere -- sampling is a fixed
// Fibonacci lattice and every tie is broken by lowest index (Seam A, #260).

#include <engine/assets/sky_gaussian/sky_gaussian.h>
#include <engine/assets/hdri/hdri_image_loader.h>

#include <math/math_types.h>

#include <vector>

namespace wz::engine::assets::sky
{
    // Equal-area unit directions via the Fibonacci spiral. Deterministic. Each
    // returned direction represents an equal solid angle of 4*pi / n.
    std::vector<wz::math::Vec3> fibonacci_sphere(int n);

    // Bilinear lat-long (equirectangular) lookup returning RGB radiance.
    //
    // Mapping (y-up):
    //   theta = atan2(dir.x, dir.z)
    //   phi   = asin(clamp(dir.y, -1, 1))
    //   u     = theta / (2*pi) + 0.5     (wrapped into [0,1))
    //   v     = 0.5 - phi / pi           (clamped to [0,1])
    // Pixels are row-major, `channels` floats per texel; RGB read from lanes
    // 0, 1, 2 (channels must be >= 3).
    struct EquirectSampler
    {
        const float* pixels = nullptr;
        int          width = 0;
        int          height = 0;
        int          channels = 0;

        wz::math::Vec3 sample(const wz::math::Vec3& dir) const;
    };

    // Fit controls. The defaults reproduce a faithful ("correct") fit; every
    // field is a hand-tunable dial, so "correct" is just a starting point, not
    // a hidden black box. Groups: structure, point sources, crispness,
    // hemisphere, and a purely creative colour grade.
    struct FitParams
    {
        // ── Structure: how many lobes and how hard to keep fitting ──
        int   target_lobes = 256;      // detail budget (now reliably reached)
        int   sample_count = 8192;     // sphere samples the fit sees
        int   refine_iterations = 8;   // coordinate-descent polish passes
        float residual_floor = 0.0f;   // stop early when the brightest residual
                                       // drops below this fraction of the start
                                       // peak (0 = fill target_lobes; higher =
                                       // fewer lobes, more abstract)

        // ── Point sources (sun / moon) pulled out before the SG fit ──
        int   point_source_count = 1;
        float point_luminance_percentile = 0.9999f;

        // ── Crispness: how sharp lobes are allowed / biased to be ──
        float sharpness_scale = 1.0f;  // multiplies the auto lambda estimate
                                       // (<1 softer/dreamier, >1 crisper)
        float min_sharpness = 4.0f;
        float max_sharpness = 4000.0f;

        // ── Hemisphere: fit the sky, ignore or de-weight the ground ──
        float horizon_elevation_deg = 0.0f; // sky/ground boundary elevation
        float ground_weight = 1.0f;         // weight for samples below it
                                            // (0 = sky-only, 1 = full sphere)

        // ── Creative grade: an intentional deviation from the source ──
        float exposure = 1.0f;               // overall brightness
        float saturation = 1.0f;             // 0 = grey, 1 = as-fit, >1 punchier
        wz::math::Vec3 tint{ 1.0f, 1.0f, 1.0f }; // per-channel colour multiply

        // ── Fit domain ──
        bool  log_domain_loss = true;
    };

    struct FitReport
    {
        float rms_tonemapped = 0.0f;
        float irradiance_error = 0.0f;
        float energy_ratio = 0.0f;
        int   lobes_placed = 0;
    };

    // Fit `p.target_lobes` spherical Gaussians (plus up to
    // `p.point_source_count` extracted point sources) to `src`. Fully
    // deterministic for a given (src, p). Optional diagnostics in `report`.
    SkyGaussianSet fit_sky_gaussians(
        const EquirectSampler& src,
        const FitParams& p,
        FitReport* report = nullptr);

    // Render a fitted set back into an equirectangular RGB (3-channel) fp32
    // panorama, inverting EquirectSampler's mapping -- for visual fit-vs-source
    // comparison (bake to EXR via save_openexr_image_to_file). Lobes are summed;
    // each point source is splatted as a small bright angular cap, floored to a
    // few texels so a near-delta sun stays visible. Deterministic. `height`
    // should be `width / 2` for a standard lat-long panorama.
    HDRImageData reconstruct_equirect(
        const SkyGaussianSet& set, int width, int height);

} // namespace wz::engine::assets::sky
