// tests/asset_gaussian_splat/gaussian_splat_decode.cpp
//
// Tests for the terrain coverage kernel evaluation function. (The former
// decode_splat() tests were removed with that helper -- the live decode is
// gaussian_splat_compilers.cpp's decode_resident_splat; see issue #277 C3-H20.)

#include <gtest/gtest.h>

#include <engine/assets/gaussian_splat/terrain_coverage_kernel.h>

#include <cmath>

namespace wz::engine::assets::test
{
    // ═══ Coverage kernel evaluation ══════════════════════════════════

    TEST(TerrainCoverageKernel, GaussianAtCenter)
    {
        // At r=0, all kernels should return 1.0 (or close to it for Gaussian).
        const float v = evaluate_coverage_kernel(
            TerrainCoverageKernelMode::Gaussian, 0.0f, 0.65f, 1.0f, 4.0f);
        EXPECT_FLOAT_EQ(v, 1.0f);
    }

    TEST(TerrainCoverageKernel, GaussianDecays)
    {
        // Gaussian should decrease with distance.
        const float v0 = evaluate_coverage_kernel(
            TerrainCoverageKernelMode::Gaussian, 0.0f, 0.65f, 1.0f, 4.0f);
        const float v1 = evaluate_coverage_kernel(
            TerrainCoverageKernelMode::Gaussian, 0.5f, 0.65f, 1.0f, 4.0f);
        const float v2 = evaluate_coverage_kernel(
            TerrainCoverageKernelMode::Gaussian, 1.0f, 0.65f, 1.0f, 4.0f);
        EXPECT_GT(v0, v1);
        EXPECT_GT(v1, v2);
        EXPECT_GT(v2, 0.0f);
    }

    TEST(TerrainCoverageKernel, GaussianMatchesFormula)
    {
        // exp(-falloff * r^2)
        const float r = 0.7f;
        const float falloff = 4.0f;
        const float expected = std::exp(-falloff * r * r);
        const float actual = evaluate_coverage_kernel(
            TerrainCoverageKernelMode::Gaussian, r, 0.65f, 1.0f, falloff);
        EXPECT_NEAR(actual, expected, 1e-6f);
    }

    TEST(TerrainCoverageKernel, SmoothDiscInsideInner)
    {
        // Inside inner_radius → 1.0
        const float v = evaluate_coverage_kernel(
            TerrainCoverageKernelMode::SmoothDisc, 0.3f, 0.65f, 1.0f, 4.0f);
        EXPECT_FLOAT_EQ(v, 1.0f);
    }

    TEST(TerrainCoverageKernel, SmoothDiscOutsideOuter)
    {
        // Outside outer_radius → 0.0
        const float v = evaluate_coverage_kernel(
            TerrainCoverageKernelMode::SmoothDisc, 1.1f, 0.65f, 1.0f, 4.0f);
        EXPECT_FLOAT_EQ(v, 0.0f);
    }

    TEST(TerrainCoverageKernel, SmoothDiscTransition)
    {
        // At midpoint between inner and outer, smoothstep gives 0.5.
        const float inner = 0.65f;
        const float outer = 1.0f;
        const float mid = (inner + outer) / 2.0f;
        const float v = evaluate_coverage_kernel(
            TerrainCoverageKernelMode::SmoothDisc, mid, inner, outer, 4.0f);
        EXPECT_NEAR(v, 0.5f, 1e-5f);
    }

    TEST(TerrainCoverageKernel, PolynomialDiscAtCenter)
    {
        const float v = evaluate_coverage_kernel(
            TerrainCoverageKernelMode::PolynomialDisc, 0.0f, 0.65f, 1.0f, 4.0f);
        EXPECT_FLOAT_EQ(v, 1.0f);
    }

    TEST(TerrainCoverageKernel, PolynomialDiscAtEdge)
    {
        // At r=1: saturate(1 - 1) = 0, 0^2 = 0
        const float v = evaluate_coverage_kernel(
            TerrainCoverageKernelMode::PolynomialDisc, 1.0f, 0.65f, 1.0f, 4.0f);
        EXPECT_FLOAT_EQ(v, 0.0f);
    }

    TEST(TerrainCoverageKernel, HardDiscInsideOne)
    {
        const float v = evaluate_coverage_kernel(
            TerrainCoverageKernelMode::HardDisc, 0.99f, 0.65f, 1.0f, 4.0f);
        EXPECT_FLOAT_EQ(v, 1.0f);
    }

    TEST(TerrainCoverageKernel, HardDiscOutsideOne)
    {
        const float v = evaluate_coverage_kernel(
            TerrainCoverageKernelMode::HardDisc, 1.01f, 0.65f, 1.0f, 4.0f);
        EXPECT_FLOAT_EQ(v, 0.0f);
    }

    TEST(TerrainCoverageKernel, NegativeRadiusTreatedAsZero)
    {
        // All kernels should handle negative r gracefully (clamp to 0).
        const float v = evaluate_coverage_kernel(
            TerrainCoverageKernelMode::Gaussian, -1.0f, 0.65f, 1.0f, 4.0f);
        EXPECT_FLOAT_EQ(v, 1.0f);  // exp(0) = 1
    }

}  // namespace
