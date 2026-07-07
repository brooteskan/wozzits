#pragma once

// engine/assets/sky_gaussian/sky_gaussian_fitter.h
//
// Deterministic, pure-CPU fit of a SkyGaussianSet to an equirectangular HDR
// panorama. No random-number generation anywhere -- sampling is a fixed
// Fibonacci lattice and every tie is broken by lowest index (Seam A, #260).

#include <engine/assets/sky_gaussian/sky_gaussian.h>

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

    struct FitParams
    {
        int   target_lobes = 256;
        int   point_source_count = 1;
        float point_luminance_percentile = 0.9999f;
        float min_sharpness = 4.0f;
        float max_sharpness = 4000.0f;
        int   sample_count = 8192;
        int   refine_iterations = 8;
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

} // namespace wz::engine::assets::sky
