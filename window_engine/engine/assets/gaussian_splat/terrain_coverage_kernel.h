#pragma once

// engine/assets/gaussian_splat/terrain_coverage_kernel.h
//
// Terrain coverage kernel: enum + CPU reference evaluation.
//
// The four kernel modes define how a normalized splat-local radius maps
// to a coverage weight in [0, 1].  This is a terrain/splat model
// concept used by:
//
//   - CPU-side terrain reconstruction (scatter-based grid evaluation)
//   - GPU coverage shaders (terrain_coverage_debug_ps.hlsl, etc.)
//   - Terrain preset / recipe settings
//   - Toolhost UI
//
// Lives in the asset layer (not gpu/) because it is a pure model
// definition with no GPU runtime dependency.  The GPU shader code
// mirrors these formulas; changes here must be reflected there.
//
// Header-only (inline) — no .cpp needed.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace wz::engine::assets
{
    // Which kernel shape evaluates per-pixel coverage from the splat-local
    // normalized radius.
    enum class TerrainCoverageKernelMode : uint32_t
    {
        // Gaussian falloff: coverage = exp(-gaussian_falloff * r²).
        // Reference behaviour matching the original 3DGS PS.
        Gaussian = 0,

        // Smoothstep disc: coverage = 1 - smoothstep(inner_radius, outer_radius, r).
        // Full interior, controlled soft edge.  Best default for terrain.
        SmoothDisc = 1,

        // Polynomial disc: coverage = saturate(1 - r²)².
        // Cheap, smooth, no sharp edge.
        PolynomialDisc = 2,

        // Hard unit disc: coverage = 1 if r ≤ 1 else 0.
        // Baseline / debug.
        HardDisc = 3,
    };

    // Evaluate the coverage kernel at normalized radius r.
    //
    // r is in [0, +inf) where 0 = splat center and 1 = splat edge
    // (before any radius_scale).  The caller is responsible for
    // computing r from world-space distance and splat extents.
    //
    // Returns a weight in [0, 1].
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
            // smoothstep: 3t² - 2t³
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

}  // namespace wz::engine::assets
