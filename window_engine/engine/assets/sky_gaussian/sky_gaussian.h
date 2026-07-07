#pragma once

// engine/assets/sky_gaussian/sky_gaussian.h
//
// Pure-CPU spherical-Gaussian (SG) sky representation. A SkyGaussianSet is a
// small mixture of (isotropic) spherical Gaussians plus a few explicit point
// sources, fit to an HDR panorama. No GPU, no asset-system registration -- this
// is the data model + evaluation only (Seam A, issue #260 / umbrella #259).

#include <math/math_types.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wz::engine::assets::sky
{
    inline constexpr float kPi = 3.14159265358979323846f;
    inline constexpr float kTwoPi = 6.28318530717958647692f;

    // A single spherical Gaussian lobe:
    //   f(w) = amplitude * exp(sharpness * (dot(direction, w) - 1))
    // direction is a unit vector; sharpness > 0 controls the angular width.
    struct SkyGaussianLobe
    {
        wz::math::Vec3 direction{ 0.0f, 1.0f, 0.0f };
        float          sharpness = 1.0f;
        wz::math::Vec3 amplitude{ 0.0f, 0.0f, 0.0f };
    };

    // A near-delta bright source (sun / moon) extracted before the SG fit.
    struct SkyPointSource
    {
        wz::math::Vec3 direction{ 0.0f, 1.0f, 0.0f };
        wz::math::Vec3 radiance{ 0.0f, 0.0f, 0.0f };
        float          solid_angle = 0.0f;
    };

    struct SkyGaussianSet
    {
        std::vector<SkyGaussianLobe> lobes;
        std::vector<SkyPointSource>  point_sources;
        std::string                  source_name;
        uint32_t                     source_width = 0;
        uint32_t                     source_height = 0;
    };

    // Evaluate one lobe in direction w (w assumed unit).
    wz::math::Vec3 evaluate_lobe(
        const SkyGaussianLobe& g, const wz::math::Vec3& w);

    // Sum of all lobes (point sources are NOT included).
    wz::math::Vec3 evaluate_set(
        const SkyGaussianSet& s, const wz::math::Vec3& w);

    // Closed-form integral of evaluate_lobe over the unit sphere:
    //   amplitude * (2*pi / sharpness) * (1 - exp(-2 * sharpness)).
    wz::math::Vec3 lobe_energy(const SkyGaussianLobe& g);

    // Rec.709 luminance of an RGB triple.
    float luminance(const wz::math::Vec3& rgb);

} // namespace wz::engine::assets::sky
