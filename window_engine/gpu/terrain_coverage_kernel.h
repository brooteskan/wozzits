#pragma once

// gpu/terrain_coverage_kernel.h
//
// CPU reference implementation of the terrain coverage kernel functions.
// These must match the GPU shader implementations (coverage PS, field
// accumulation PS) so that CPU-side reconstruction produces results
// consistent with GPU rendering.
//
// The four kernel modes are defined in gaussian_splat_coverage_settings.h
// as TerrainCoverageKernelMode.  This header provides the evaluation
// function that maps a normalized radius to a coverage weight.
//
// Header-only (inline) to avoid adding a .cpp to the GPU library for
// a single pure function.

#include <gpu/gaussian_splat_coverage_settings.h>

#include <algorithm>
#include <cmath>

namespace wz::gpu
{
    // Evaluate the coverage kernel at normalized radius r.
    //
    // r is in [0, +inf) where 0 = splat center and 1 = splat edge
    // (before any radius_scale).  The caller is responsible for
    // computing r from world-space distance and splat extents.
    //
    // Returns a weight in [0, 1].
    //
    // Parameters:
    //   mode            Which kernel shape to evaluate.
    //   r               Normalized distance from splat center.
    //   inner_radius    SmoothDisc: full-coverage inner boundary [0,1].
    //   outer_radius    SmoothDisc: zero-coverage outer boundary [0,1].
    //   gaussian_falloff  Gaussian: controls width of the bell curve.
    inline float evaluate_coverage_kernel(
        TerrainCoverageKernelMode mode,
        float r,
        float inner_radius,
        float outer_radius,
        float gaussian_falloff) noexcept
    {
        if (r < 0.0f) r = 0.0f;

        switch (mode)
        {
        case TerrainCoverageKernelMode::Gaussian:
            return std::exp(-gaussian_falloff * r * r);

        case TerrainCoverageKernelMode::SmoothDisc:
        {
            if (r <= inner_radius) return 1.0f;
            if (r >= outer_radius) return 0.0f;
            const float t = (r - inner_radius) / (outer_radius - inner_radius);
            // smoothstep: 3t^2 - 2t^3
            return 1.0f - (3.0f * t * t - 2.0f * t * t * t);
        }

        case TerrainCoverageKernelMode::PolynomialDisc:
        {
            const float t = std::clamp(1.0f - r * r, 0.0f, 1.0f);
            return t * t;
        }

        case TerrainCoverageKernelMode::HardDisc:
            return (r <= 1.0f) ? 1.0f : 0.0f;

        default:
            return 0.0f;
        }
    }

}  // namespace wz::gpu
