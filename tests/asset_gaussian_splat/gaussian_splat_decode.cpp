// tests/asset_gaussian_splat/gaussian_splat_decode.cpp
//
// Tests for the shared decode_splat() helper and the coverage kernel
// evaluation function.

#include <gtest/gtest.h>

#include <engine/assets/gaussian_splat/gaussian_splat_decode.h>
#include <engine/assets/gaussian_splat/terrain_coverage_kernel.h>
#include <engine/assets/gaussian_splat/gaussian_splat_cloud.h>

#include <cmath>

namespace wz::engine::assets::test
{
    namespace
    {
        constexpr float kSH_C0 = 0.28209479177387814f;

        // Build a GaussianSplat with identity rotation, unit scale (log),
        // full opacity (logit), and neutral gray (SH DC).
        GaussianSplat make_identity_splat()
        {
            GaussianSplat s{};
            s.position[0] = 1.0f;
            s.position[1] = 2.0f;
            s.position[2] = 3.0f;

            // log(1.0) = 0
            s.scale[0] = 0.0f;
            s.scale[1] = 0.0f;
            s.scale[2] = 0.0f;

            // PLY convention: rotation[0]=w, [1]=x, [2]=y, [3]=z.
            // Identity quaternion: w=1, x=y=z=0.
            s.rotation[0] = 1.0f;
            s.rotation[1] = 0.0f;
            s.rotation[2] = 0.0f;
            s.rotation[3] = 0.0f;

            // logit(0.5) = 0
            s.opacity = 0.0f;

            // SH DC for gray 0.5: (0.5 - 0.5) / kSH_C0 = 0
            s.color_dc[0] = 0.0f;
            s.color_dc[1] = 0.0f;
            s.color_dc[2] = 0.0f;

            return s;
        }
    }

    // ─── Scale decoding ──────────────────────────────────────────────

    TEST(GaussianSplatDecode, ScaleExpDecode)
    {
        GaussianSplat s = make_identity_splat();
        s.scale[0] = std::log(2.0f);
        s.scale[1] = std::log(0.5f);
        s.scale[2] = std::log(3.0f);

        const auto d = decode_splat(s);
        EXPECT_NEAR(d.scale.x, 2.0f,  1e-5f);
        EXPECT_NEAR(d.scale.y, 0.5f,  1e-5f);
        EXPECT_NEAR(d.scale.z, 3.0f,  1e-5f);
    }

    TEST(GaussianSplatDecode, ScaleZeroMeansUnitScale)
    {
        GaussianSplat s = make_identity_splat();
        // log(1) = 0 for all axes
        const auto d = decode_splat(s);
        EXPECT_NEAR(d.scale.x, 1.0f, 1e-6f);
        EXPECT_NEAR(d.scale.y, 1.0f, 1e-6f);
        EXPECT_NEAR(d.scale.z, 1.0f, 1e-6f);
    }

    // ─── Opacity decoding ────────────────────────────────────────────

    TEST(GaussianSplatDecode, OpacitySigmoid)
    {
        GaussianSplat s = make_identity_splat();

        // logit(0.5) = 0 → sigmoid(0) = 0.5
        s.opacity = 0.0f;
        EXPECT_NEAR(decode_splat(s).opacity, 0.5f, 1e-6f);

        // logit(~0.73) = 1.0 → sigmoid(1) ≈ 0.7311
        s.opacity = 1.0f;
        EXPECT_NEAR(decode_splat(s).opacity, 1.0f / (1.0f + std::exp(-1.0f)), 1e-5f);

        // Large positive logit → opacity near 1
        s.opacity = 10.0f;
        EXPECT_GT(decode_splat(s).opacity, 0.999f);

        // Large negative logit → opacity near 0
        s.opacity = -10.0f;
        EXPECT_LT(decode_splat(s).opacity, 0.001f);
    }

    // ─── Color decoding (SH DC → display RGB) ───────────────────────

    TEST(GaussianSplatDecode, ColorSHDCDecode)
    {
        GaussianSplat s = make_identity_splat();

        // SH DC = 0 → display = 0.5
        EXPECT_NEAR(decode_splat(s).color.x, 0.5f, 1e-6f);

        // SH DC that maps to display 0.8:
        //   display = sh_dc * kSH_C0 + 0.5
        //   sh_dc = (0.8 - 0.5) / kSH_C0
        const float sh_dc_08 = (0.8f - 0.5f) / kSH_C0;
        s.color_dc[0] = sh_dc_08;
        s.color_dc[1] = sh_dc_08;
        s.color_dc[2] = sh_dc_08;
        const auto d = decode_splat(s);
        EXPECT_NEAR(d.color.x, 0.8f, 1e-4f);
        EXPECT_NEAR(d.color.y, 0.8f, 1e-4f);
        EXPECT_NEAR(d.color.z, 0.8f, 1e-4f);
    }

    TEST(GaussianSplatDecode, ColorClampedToUnitRange)
    {
        GaussianSplat s = make_identity_splat();

        // Very large SH DC → clamped to 1.0
        s.color_dc[0] = 100.0f;
        EXPECT_FLOAT_EQ(decode_splat(s).color.x, 1.0f);

        // Very negative SH DC → clamped to 0.0
        s.color_dc[0] = -100.0f;
        EXPECT_FLOAT_EQ(decode_splat(s).color.x, 0.0f);
    }

    // ─── Rotation convention swizzle ─────────────────────────────────

    TEST(GaussianSplatDecode, RotationPLYToEngineConvention)
    {
        GaussianSplat s = make_identity_splat();
        // PLY: [w, x, y, z]
        s.rotation[0] = 0.707f;  // w
        s.rotation[1] = 0.1f;    // x
        s.rotation[2] = 0.2f;    // y
        s.rotation[3] = 0.3f;    // z

        const auto d = decode_splat(s);
        // Engine Quaternion: {x, y, z, w}
        EXPECT_NEAR(d.rotation.x, 0.1f,   1e-6f);
        EXPECT_NEAR(d.rotation.y, 0.2f,   1e-6f);
        EXPECT_NEAR(d.rotation.z, 0.3f,   1e-6f);
        EXPECT_NEAR(d.rotation.w, 0.707f, 1e-6f);
    }

    // ─── Axis extraction ─────────────────────────────────────────────

    TEST(GaussianSplatDecode, IdentityQuatGivesStandardAxes)
    {
        GaussianSplat s = make_identity_splat();
        // PLY identity: rotation = [1, 0, 0, 0] (w=1, x=y=z=0)
        const auto d = decode_splat(s);

        // X axis = (1, 0, 0)
        EXPECT_NEAR(d.axis_x.x, 1.0f, 1e-6f);
        EXPECT_NEAR(d.axis_x.y, 0.0f, 1e-6f);
        EXPECT_NEAR(d.axis_x.z, 0.0f, 1e-6f);

        // Y axis = (0, 1, 0)
        EXPECT_NEAR(d.axis_y.x, 0.0f, 1e-6f);
        EXPECT_NEAR(d.axis_y.y, 1.0f, 1e-6f);
        EXPECT_NEAR(d.axis_y.z, 0.0f, 1e-6f);

        // Z axis = (0, 0, 1)
        EXPECT_NEAR(d.axis_z.x, 0.0f, 1e-6f);
        EXPECT_NEAR(d.axis_z.y, 0.0f, 1e-6f);
        EXPECT_NEAR(d.axis_z.z, 1.0f, 1e-6f);
    }

    TEST(GaussianSplatDecode, Rotate90AroundY_SwapsXZ)
    {
        GaussianSplat s = make_identity_splat();
        // 90 degrees around Y: q = (cos(45), 0, sin(45), 0)
        // PLY: [w, x, y, z] = [cos(45), 0, sin(45), 0]
        const float c = std::cos(3.14159265f / 4.0f);
        const float sn = std::sin(3.14159265f / 4.0f);
        s.rotation[0] = c;    // w
        s.rotation[1] = 0.0f; // x
        s.rotation[2] = sn;   // y
        s.rotation[3] = 0.0f; // z

        const auto d = decode_splat(s);

        // After 90-deg rotation around Y:
        //   X axis → (0, 0, -1)  (former X now points to -Z)
        //   Y axis → (0, 1, 0)   (unchanged)
        //   Z axis → (1, 0, 0)   (former Z now points to +X)
        EXPECT_NEAR(d.axis_x.x,  0.0f, 1e-5f);
        EXPECT_NEAR(d.axis_x.y,  0.0f, 1e-5f);
        EXPECT_NEAR(d.axis_x.z, -1.0f, 1e-5f);

        EXPECT_NEAR(d.axis_y.x, 0.0f, 1e-5f);
        EXPECT_NEAR(d.axis_y.y, 1.0f, 1e-5f);
        EXPECT_NEAR(d.axis_y.z, 0.0f, 1e-5f);

        EXPECT_NEAR(d.axis_z.x, 1.0f, 1e-5f);
        EXPECT_NEAR(d.axis_z.y, 0.0f, 1e-5f);
        EXPECT_NEAR(d.axis_z.z, 0.0f, 1e-5f);
    }

    // ─── Position passthrough ────────────────────────────────────────

    TEST(GaussianSplatDecode, PositionPassthrough)
    {
        GaussianSplat s = make_identity_splat();
        s.position[0] = -5.0f;
        s.position[1] = 10.0f;
        s.position[2] = 42.0f;

        const auto d = decode_splat(s);
        EXPECT_FLOAT_EQ(d.position.x, -5.0f);
        EXPECT_FLOAT_EQ(d.position.y, 10.0f);
        EXPECT_FLOAT_EQ(d.position.z, 42.0f);
    }


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
