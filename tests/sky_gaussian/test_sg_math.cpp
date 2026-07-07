#include <gtest/gtest.h>

#include <engine/assets/sky_gaussian/sky_gaussian.h>
#include <engine/assets/sky_gaussian/sky_gaussian_fitter.h>

#include <math/vec3.h>

using namespace wz::engine::assets::sky;
using wz::math::Vec3;

TEST(SkyGaussianMath, EvaluateAtCenterEqualsAmplitude)
{
    SkyGaussianLobe g;
    g.direction = wz::math::normalize(Vec3{ 0.3f, 1.0f, -0.2f });
    g.sharpness = 40.0f;
    g.amplitude = Vec3{ 1.5f, 2.0f, 0.5f };

    const Vec3 v = evaluate_lobe(g, g.direction);
    EXPECT_NEAR(v.x, 1.5f, 1e-4f);
    EXPECT_NEAR(v.y, 2.0f, 1e-4f);
    EXPECT_NEAR(v.z, 0.5f, 1e-4f);
}

TEST(SkyGaussianMath, LobeEnergyMatchesNumericIntegral)
{
    SkyGaussianLobe g;
    g.direction = wz::math::normalize(Vec3{ 0.0f, 1.0f, 0.0f });
    g.sharpness = 8.0f;
    g.amplitude = Vec3{ 1.0f, 0.6f, 0.3f };

    const auto dirs = fibonacci_sphere(16384);
    const float omega = 4.0f * kPi / static_cast<float>(dirs.size());

    Vec3 numeric{ 0.0f, 0.0f, 0.0f };
    for (const auto& w : dirs) {
        numeric = numeric + evaluate_lobe(g, w) * omega;
    }

    const Vec3 analytic = lobe_energy(g);

    // A few-percent tolerance covers the Fibonacci quadrature error.
    EXPECT_NEAR(numeric.x, analytic.x, 0.02f * analytic.x + 1e-4f);
    EXPECT_NEAR(numeric.y, analytic.y, 0.02f * analytic.y + 1e-4f);
    EXPECT_NEAR(numeric.z, analytic.z, 0.02f * analytic.z + 1e-4f);
    EXPECT_NEAR(luminance(numeric), luminance(analytic),
        0.02f * luminance(analytic) + 1e-4f);
}

TEST(SkyGaussianMath, FibonacciSphereWeightsAndUnit)
{
    const int n = 4096;
    const auto dirs = fibonacci_sphere(n);
    ASSERT_EQ(static_cast<int>(dirs.size()), n);

    const float omega = 4.0f * kPi / static_cast<float>(n);
    float sum_omega = 0.0f;
    Vec3 mean{ 0.0f, 0.0f, 0.0f };
    for (const auto& d : dirs) {
        EXPECT_NEAR(wz::math::length(d), 1.0f, 1e-4f);
        sum_omega += omega;
        mean = mean + d;
    }

    // Solid angles sum to the full sphere.
    EXPECT_NEAR(sum_omega, 4.0f * kPi, 1e-2f);

    // The lattice is well balanced -> mean direction near zero.
    mean = mean * (1.0f / static_cast<float>(n));
    EXPECT_LT(wz::math::length(mean), 0.05f);
}
