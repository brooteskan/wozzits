// tests/asset_gaussian_splat/terrain_surface_coverage.cpp
//
// Regression tests for terrain splat surface coverage behavior.
//
// These verify that the terrain surface compiler produces correctly-
// oriented, correctly-scaled splats across six canonical heightfield
// shapes.  Where the handedness tests (terrain_surface_frame_handedness)
// prove the frame math is right, these tests prove the VISUAL coverage
// behavior: splats cover the surface, normals point outward, scales
// are bounded, and the preset parameters produce consistent results.
//
// Fixtures:
//   flat plane    — baseline; discs should be uniform, normal = +Y
//   X ramp        — pure U slope
//   Z ramp        — pure V slope
//   diagonal ramp — combined U+V slope
//   ridge         — two opposing ramps meeting at a peak (convexity)
//   bowl          — concave paraboloid (concavity)
//
// All fixtures use the kSmoothTerrainSurface preset so these tests
// double as regression for the known-good parameter set.

#include <gtest/gtest.h>

#include <engine/assets/gaussian_splat/gaussian_splat.h>
#include <engine/assets/gaussian_splat/gaussian_splat_compilers.h>
#include <engine/assets/gaussian_splat/gaussian_splat_terrain_preset.h>
#include <engine/assets/scalar_field/scalar_field.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace wz::engine::assets::test
{
    namespace
    {
        // ─── Math helpers ───────────────────────────────────────────────

        struct Vec3 { float x, y, z; };

        struct Mat3 { Vec3 col0, col1, col2; };

        Mat3 quat_to_matrix(const float r[4])
        {
            const float w = r[0], x = r[1], y = r[2], z = r[3];
            return {
                Vec3{ 1 - 2*(y*y + z*z), 2*(x*y + w*z), 2*(x*z - w*y) },
                Vec3{ 2*(x*y - w*z), 1 - 2*(x*x + z*z), 2*(y*z + w*x) },
                Vec3{ 2*(x*z + w*y), 2*(y*z - w*x), 1 - 2*(x*x + y*y) },
            };
        }

        float determinant(const Mat3& m)
        {
            const Vec3 c{
                m.col1.y * m.col2.z - m.col1.z * m.col2.y,
                m.col1.z * m.col2.x - m.col1.x * m.col2.z,
                m.col1.x * m.col2.y - m.col1.y * m.col2.x,
            };
            return m.col0.x*c.x + m.col0.y*c.y + m.col0.z*c.z;
        }

        // ─── Fixture factory ────────────────────────────────────────────

        constexpr uint32_t kFixtureSize = 32;

        // Apply the smooth-terrain preset to a compile desc with unit
        // grid spacing and unit height scale.
        GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc
        make_preset_desc()
        {
            GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc desc{};
            desc.step_x        = 1.0f;
            desc.step_z        = 1.0f;
            desc.height_scale  = 1.0f;
            kSmoothTerrainSurface.apply_to(desc);
            return desc;
        }

        ScalarFieldData make_field(uint32_t size = kFixtureSize)
        {
            ScalarFieldData f{};
            f.width  = size;
            f.height = size;
            f.depth  = 1;
            f.format = ScalarFieldFormat::Float32;
            f.domain_kind = ScalarFieldDomainKind::Spatial2D;
            f.values.resize(size * size, 0.0f);
            f.min_value = 0.0f;
            f.max_value = 0.0f;
            return f;
        }

        void update_minmax(ScalarFieldData& f)
        {
            float mn = f.values[0], mx = f.values[0];
            for (float v : f.values) {
                if (v < mn) mn = v;
                if (v > mx) mx = v;
            }
            f.min_value = mn;
            f.max_value = mx;
        }

        // Flat plane at h = 0.
        ScalarFieldData make_flat()
        {
            return make_field();
        }

        // Constant ramp in +X: h(ix, iz) = ix * 0.5.
        ScalarFieldData make_x_ramp()
        {
            auto f = make_field();
            for (uint32_t iz = 0; iz < f.height; ++iz)
                for (uint32_t ix = 0; ix < f.width; ++ix)
                    f.values[ix + iz * f.width] = ix * 0.5f;
            update_minmax(f);
            return f;
        }

        // Constant ramp in +Z: h(ix, iz) = iz * 0.5.
        ScalarFieldData make_z_ramp()
        {
            auto f = make_field();
            for (uint32_t iz = 0; iz < f.height; ++iz)
                for (uint32_t ix = 0; ix < f.width; ++ix)
                    f.values[ix + iz * f.width] = iz * 0.5f;
            update_minmax(f);
            return f;
        }

        // Diagonal ramp: h(ix, iz) = (ix + iz) * 0.3.
        ScalarFieldData make_diagonal_ramp()
        {
            auto f = make_field();
            for (uint32_t iz = 0; iz < f.height; ++iz)
                for (uint32_t ix = 0; ix < f.width; ++ix)
                    f.values[ix + iz * f.width] = (ix + iz) * 0.3f;
            update_minmax(f);
            return f;
        }

        // Ridge: two ramps meeting at center column.
        //   h(ix, iz) = max_height - |ix - center| * slope
        // Sharp convexity at ix = center.
        ScalarFieldData make_ridge()
        {
            auto f = make_field();
            const float center = (f.width - 1) * 0.5f;
            const float slope  = 0.4f;
            const float peak   = center * slope;
            for (uint32_t iz = 0; iz < f.height; ++iz)
                for (uint32_t ix = 0; ix < f.width; ++ix)
                    f.values[ix + iz * f.width] =
                        peak - std::abs(static_cast<float>(ix) - center) * slope;
            update_minmax(f);
            return f;
        }

        // Bowl: concave paraboloid centered at grid center.
        //   h(ix, iz) = ((ix-cx)^2 + (iz-cz)^2) * scale
        ScalarFieldData make_bowl()
        {
            auto f = make_field();
            const float cx = (f.width - 1) * 0.5f;
            const float cz = (f.height - 1) * 0.5f;
            const float scale = 0.02f;
            for (uint32_t iz = 0; iz < f.height; ++iz)
                for (uint32_t ix = 0; ix < f.width; ++ix)
                {
                    const float dx = ix - cx;
                    const float dz = iz - cz;
                    f.values[ix + iz * f.width] = (dx*dx + dz*dz) * scale;
                }
            update_minmax(f);
            return f;
        }

        // ─── Common per-cloud checks ────────────────────────────────────

        // Verify invariants that hold for ALL terrain clouds compiled with
        // the smooth-terrain preset: right-handed frame, bounded scales,
        // outward-facing normal.
        void check_common_invariants(
            const GaussianSplatCloudData& cloud,
            const char* fixture_name)
        {
            SCOPED_TRACE(fixture_name);
            ASSERT_TRUE(cloud.valid());
            ASSERT_FALSE(cloud.splats.empty());

            // Bounds must be valid.
            EXPECT_TRUE(cloud.bounds.valid);

            for (size_t i = 0; i < cloud.splats.size(); ++i)
            {
                SCOPED_TRACE("splat " + std::to_string(i));
                const auto& s = cloud.splats[i];
                const Mat3 m = quat_to_matrix(s.rotation);

                // Right-handed frame (determinant +1).
                EXPECT_NEAR(determinant(m), 1.0f, 1e-3f);

                // Normal (column 1) must point "upward" — positive Y
                // component — for any terrain surface.  The normal might
                // tilt but should never point downward.
                EXPECT_GT(m.col1.y, 0.0f)
                    << "surface normal Y component should be positive";

                // Position must be inside bounds.
                EXPECT_GE(s.position[0], cloud.bounds.min[0]);
                EXPECT_LE(s.position[0], cloud.bounds.max[0]);
                EXPECT_GE(s.position[1], cloud.bounds.min[1]);
                EXPECT_LE(s.position[1], cloud.bounds.max[1]);
                EXPECT_GE(s.position[2], cloud.bounds.min[2]);
                EXPECT_LE(s.position[2], cloud.bounds.max[2]);

                // Log-scales must be finite.
                EXPECT_TRUE(std::isfinite(s.scale[0]));
                EXPECT_TRUE(std::isfinite(s.scale[1]));
                EXPECT_TRUE(std::isfinite(s.scale[2]));
            }
        }

        // Check that tangent-plane scales (scale[0] and scale[2]) are
        // bounded: not too small (gaps) and not too large (extreme
        // elongation).  Expects log-encoded values.
        void check_scale_bounds(
            const GaussianSplatCloudData& cloud,
            float step,
            float overlap,
            float max_slope_stretch,
            const char* fixture_name)
        {
            SCOPED_TRACE(fixture_name);

            const float expected_flat = step * overlap;
            const float max_tangent   = step * overlap * max_slope_stretch;

            // Allow 10% tolerance for floating point.
            const float log_min = std::log(expected_flat * 0.5f);
            const float log_max = std::log(max_tangent * 1.1f);

            for (size_t i = 0; i < cloud.splats.size(); ++i)
            {
                SCOPED_TRACE("splat " + std::to_string(i));
                const auto& s = cloud.splats[i];

                // scale[0] = tangent U, scale[2] = tangent V
                EXPECT_GE(s.scale[0], log_min)
                    << "tangent-U scale too small (gaps)";
                EXPECT_LE(s.scale[0], log_max)
                    << "tangent-U scale too large (elongation)";
                EXPECT_GE(s.scale[2], log_min)
                    << "tangent-V scale too small (gaps)";
                EXPECT_LE(s.scale[2], log_max)
                    << "tangent-V scale too large (elongation)";
            }
        }

        // Check that the aspect ratio (max tangent scale / min tangent
        // scale) per splat is bounded.
        void check_aspect_ratio(
            const GaussianSplatCloudData& cloud,
            float max_ratio,
            const char* fixture_name)
        {
            SCOPED_TRACE(fixture_name);

            for (size_t i = 0; i < cloud.splats.size(); ++i)
            {
                SCOPED_TRACE("splat " + std::to_string(i));
                const auto& s = cloud.splats[i];

                const float su = std::exp(s.scale[0]);
                const float sv = std::exp(s.scale[2]);
                const float ratio = (su > sv) ? su / sv : sv / su;
                EXPECT_LE(ratio, max_ratio + 0.01f)
                    << "aspect ratio exceeds max_slope_stretch";
            }
        }
    }  // anon namespace


    // ─── Flat plane ─────────────────────────────────────────────────────

    TEST(TerrainSurfaceCoverage, FlatPlane_CommonInvariants)
    {
        const auto desc  = make_preset_desc();
        const auto field = make_flat();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        check_common_invariants(cloud, "flat");
    }

    TEST(TerrainSurfaceCoverage, FlatPlane_UniformNormal)
    {
        const auto desc  = make_preset_desc();
        const auto field = make_flat();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        ASSERT_TRUE(cloud.valid());

        for (const auto& s : cloud.splats)
        {
            const Mat3 m = quat_to_matrix(s.rotation);
            // All normals should be exactly +Y on a flat plane.
            EXPECT_NEAR(m.col1.x, 0.0f, 1e-3f);
            EXPECT_NEAR(m.col1.y, 1.0f, 1e-3f);
            EXPECT_NEAR(m.col1.z, 0.0f, 1e-3f);
        }
    }

    TEST(TerrainSurfaceCoverage, FlatPlane_UniformScale)
    {
        const auto desc  = make_preset_desc();
        const auto field = make_flat();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        ASSERT_TRUE(cloud.valid());

        // On a flat plane with step=1 and overlap=1.5, every splat
        // should have the same tangent scale: log(1 * 1.5) = log(1.5).
        const float expected = std::log(desc.step_x * desc.overlap_factor);
        for (const auto& s : cloud.splats)
        {
            EXPECT_NEAR(s.scale[0], expected, 0.01f);
            EXPECT_NEAR(s.scale[2], expected, 0.01f);
        }
    }

    TEST(TerrainSurfaceCoverage, FlatPlane_SplatCount)
    {
        const auto desc  = make_preset_desc();
        const auto field = make_flat();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        ASSERT_TRUE(cloud.valid());

        // With subsample_step=1 and field 32x32, expect 32*32=1024 splats.
        EXPECT_EQ(cloud.splats.size(), kFixtureSize * kFixtureSize);
    }


    // ─── X ramp ─────────────────────────────────────────────────────────

    TEST(TerrainSurfaceCoverage, XRamp_CommonInvariants)
    {
        const auto desc  = make_preset_desc();
        const auto field = make_x_ramp();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        check_common_invariants(cloud, "x_ramp");
    }

    TEST(TerrainSurfaceCoverage, XRamp_ScaleBounds)
    {
        const auto desc  = make_preset_desc();
        const auto field = make_x_ramp();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        check_scale_bounds(cloud, desc.step_x, desc.overlap_factor,
            desc.max_slope_stretch, "x_ramp");
    }

    TEST(TerrainSurfaceCoverage, XRamp_NormalTiltsTowardMinusX)
    {
        const auto desc  = make_preset_desc();
        const auto field = make_x_ramp();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        ASSERT_TRUE(cloud.valid());

        // Interior splats should have normals tilting toward -X.
        // (Slope rises in +X → normal tilts toward -X.)
        const uint32_t W = field.width;
        for (uint32_t iz = 2; iz < field.height - 2; ++iz)
        {
            for (uint32_t ix = 2; ix < W - 2; ++ix)
            {
                const auto& s = cloud.splats[ix + iz * W];
                const Mat3 m = quat_to_matrix(s.rotation);
                EXPECT_LT(m.col1.x, -0.1f)
                    << "at (" << ix << "," << iz << ")";
            }
        }
    }


    // ─── Z ramp ─────────────────────────────────────────────────────────

    TEST(TerrainSurfaceCoverage, ZRamp_CommonInvariants)
    {
        const auto desc  = make_preset_desc();
        const auto field = make_z_ramp();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        check_common_invariants(cloud, "z_ramp");
    }

    TEST(TerrainSurfaceCoverage, ZRamp_ScaleBounds)
    {
        const auto desc  = make_preset_desc();
        const auto field = make_z_ramp();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        check_scale_bounds(cloud, desc.step_z, desc.overlap_factor,
            desc.max_slope_stretch, "z_ramp");
    }

    TEST(TerrainSurfaceCoverage, ZRamp_NormalTiltsTowardMinusZ)
    {
        const auto desc  = make_preset_desc();
        const auto field = make_z_ramp();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        ASSERT_TRUE(cloud.valid());

        const uint32_t W = field.width;
        for (uint32_t iz = 2; iz < field.height - 2; ++iz)
        {
            for (uint32_t ix = 2; ix < W - 2; ++ix)
            {
                const auto& s = cloud.splats[ix + iz * W];
                const Mat3 m = quat_to_matrix(s.rotation);
                EXPECT_LT(m.col1.z, -0.1f)
                    << "at (" << ix << "," << iz << ")";
            }
        }
    }


    // ─── Diagonal ramp ──────────────────────────────────────────────────

    TEST(TerrainSurfaceCoverage, DiagonalRamp_CommonInvariants)
    {
        const auto desc  = make_preset_desc();
        const auto field = make_diagonal_ramp();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        check_common_invariants(cloud, "diagonal_ramp");
    }

    TEST(TerrainSurfaceCoverage, DiagonalRamp_ScaleBounds)
    {
        const auto desc  = make_preset_desc();
        const auto field = make_diagonal_ramp();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        check_scale_bounds(cloud, desc.step_x, desc.overlap_factor,
            desc.max_slope_stretch, "diagonal_ramp");
    }

    TEST(TerrainSurfaceCoverage, DiagonalRamp_AspectRatio)
    {
        const auto desc  = make_preset_desc();
        const auto field = make_diagonal_ramp();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        check_aspect_ratio(cloud, desc.max_slope_stretch, "diagonal_ramp");
    }


    // ─── Ridge (convexity) ──────────────────────────────────────────────

    TEST(TerrainSurfaceCoverage, Ridge_CommonInvariants)
    {
        const auto desc  = make_preset_desc();
        const auto field = make_ridge();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        check_common_invariants(cloud, "ridge");
    }

    TEST(TerrainSurfaceCoverage, Ridge_ScaleBounds)
    {
        const auto desc  = make_preset_desc();
        const auto field = make_ridge();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        check_scale_bounds(cloud, desc.step_x, desc.overlap_factor,
            desc.max_slope_stretch, "ridge");
    }

    TEST(TerrainSurfaceCoverage, Ridge_PeakNormalsPointUp)
    {
        const auto desc  = make_preset_desc();
        const auto field = make_ridge();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        ASSERT_TRUE(cloud.valid());

        // At the ridge peak (center column), the two slopes meet.
        // The raw normal points straight up (slope gradient = 0 at
        // the exact peak).  With smoothing, the peak normal should
        // still be predominantly +Y.
        const uint32_t W = field.width;
        const uint32_t center_ix = W / 2;
        for (uint32_t iz = 2; iz < field.height - 2; ++iz)
        {
            const auto& s = cloud.splats[center_ix + iz * W];
            const Mat3 m = quat_to_matrix(s.rotation);
            EXPECT_GT(m.col1.y, 0.5f)
                << "peak normal should be predominantly +Y at iz=" << iz;
        }
    }

    TEST(TerrainSurfaceCoverage, Ridge_OpposingSlopeNormals)
    {
        const auto desc  = make_preset_desc();
        const auto field = make_ridge();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        ASSERT_TRUE(cloud.valid());

        // Left of peak (ix < center): height rises toward center, so the
        // surface faces outward (away from peak).  Normal tilts toward -X.
        // Right of peak (ix > center): same logic, normal tilts toward +X.
        const uint32_t W = field.width;
        const uint32_t iz_mid = field.height / 2;

        // Left side: surface faces outward (toward -X).
        {
            const auto& s = cloud.splats[4 + iz_mid * W];
            const Mat3 m = quat_to_matrix(s.rotation);
            EXPECT_LT(m.col1.x, -0.1f)
                << "left-of-peak normal should tilt -X (facing away from peak)";
        }
        // Right side: surface faces outward (toward +X).
        {
            const auto& s = cloud.splats[(W - 5) + iz_mid * W];
            const Mat3 m = quat_to_matrix(s.rotation);
            EXPECT_GT(m.col1.x, 0.1f)
                << "right-of-peak normal should tilt +X (facing away from peak)";
        }
    }

    TEST(TerrainSurfaceCoverage, Ridge_AspectRatio)
    {
        const auto desc  = make_preset_desc();
        const auto field = make_ridge();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        check_aspect_ratio(cloud, desc.max_slope_stretch, "ridge");
    }


    // ─── Bowl (concavity) ───────────────────────────────────────────────

    TEST(TerrainSurfaceCoverage, Bowl_CommonInvariants)
    {
        const auto desc  = make_preset_desc();
        const auto field = make_bowl();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        check_common_invariants(cloud, "bowl");
    }

    TEST(TerrainSurfaceCoverage, Bowl_ScaleBounds)
    {
        const auto desc  = make_preset_desc();
        const auto field = make_bowl();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        check_scale_bounds(cloud, desc.step_x, desc.overlap_factor,
            desc.max_slope_stretch, "bowl");
    }

    TEST(TerrainSurfaceCoverage, Bowl_CenterNormalPointsUp)
    {
        const auto desc  = make_preset_desc();
        const auto field = make_bowl();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        ASSERT_TRUE(cloud.valid());

        // Center of the bowl: gradient = 0, normal = +Y.
        const uint32_t W = field.width;
        const uint32_t cx = W / 2;
        const uint32_t cz = field.height / 2;
        const auto& s = cloud.splats[cx + cz * W];
        const Mat3 m = quat_to_matrix(s.rotation);
        EXPECT_NEAR(m.col1.x, 0.0f, 0.05f);
        EXPECT_NEAR(m.col1.y, 1.0f, 0.05f);
        EXPECT_NEAR(m.col1.z, 0.0f, 0.05f);
    }

    TEST(TerrainSurfaceCoverage, Bowl_NormalsPointOutward)
    {
        const auto desc  = make_preset_desc();
        const auto field = make_bowl();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        ASSERT_TRUE(cloud.valid());

        // Bowl (concave up): height increases away from center, so the
        // surface at the rim faces INWARD (toward center).
        // At (ix=3, iz=center): normal tilts toward +X (toward center).
        // At (ix=W-4, iz=center): normal tilts toward -X (toward center).
        // At (ix=center, iz=3): normal tilts toward +Z (toward center).
        // At (ix=center, iz=H-4): normal tilts toward -Z (toward center).
        const uint32_t W = field.width;
        const uint32_t H = field.height;
        const uint32_t cx = W / 2;
        const uint32_t cz = H / 2;

        {
            const auto& s = cloud.splats[3 + cz * W];
            const Mat3 m = quat_to_matrix(s.rotation);
            EXPECT_GT(m.col1.x, 0.05f) << "left-rim normal should tilt +X (toward center)";
        }
        {
            const auto& s = cloud.splats[(W - 4) + cz * W];
            const Mat3 m = quat_to_matrix(s.rotation);
            EXPECT_LT(m.col1.x, -0.05f) << "right-rim normal should tilt -X (toward center)";
        }
        {
            const auto& s = cloud.splats[cx + 3 * W];
            const Mat3 m = quat_to_matrix(s.rotation);
            EXPECT_GT(m.col1.z, 0.05f) << "top-rim normal should tilt +Z (toward center)";
        }
        {
            const auto& s = cloud.splats[cx + (H - 4) * W];
            const Mat3 m = quat_to_matrix(s.rotation);
            EXPECT_LT(m.col1.z, -0.05f) << "bottom-rim normal should tilt -Z (toward center)";
        }
    }

    TEST(TerrainSurfaceCoverage, Bowl_AspectRatio)
    {
        const auto desc  = make_preset_desc();
        const auto field = make_bowl();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        check_aspect_ratio(cloud, desc.max_slope_stretch, "bowl");
    }


    // ─── Preset consistency ─────────────────────────────────────────────

    TEST(TerrainSurfaceCoverage, PresetApplyRoundTrip)
    {
        // Verify that apply_to correctly transfers all preset fields.
        GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc desc{};
        desc.step_x       = 0.5f;
        desc.step_z       = 0.75f;
        desc.height_scale = 42.0f;

        kSmoothTerrainSurface.apply_to(desc);

        // Data-dependent fields must be untouched.
        EXPECT_FLOAT_EQ(desc.step_x, 0.5f);
        EXPECT_FLOAT_EQ(desc.step_z, 0.75f);
        EXPECT_FLOAT_EQ(desc.height_scale, 42.0f);

        // Preset fields must match.
        EXPECT_FLOAT_EQ(desc.overlap_factor,    kSmoothTerrainSurface.overlap_factor);
        EXPECT_FLOAT_EQ(desc.max_slope_stretch, kSmoothTerrainSurface.max_slope_stretch);
        EXPECT_FLOAT_EQ(desc.opacity,           kSmoothTerrainSurface.opacity);
        EXPECT_EQ(desc.subsample_step,          kSmoothTerrainSurface.subsample_step);
        EXPECT_EQ(desc.normal_smoothing_enabled,
            kSmoothTerrainSurface.normal_smoothing_enabled);
        EXPECT_EQ(desc.normal_smoothing_radius_cells,
            kSmoothTerrainSurface.normal_smoothing_radius_cells);
        EXPECT_FLOAT_EQ(desc.normal_smoothing_sigma_cells,
            kSmoothTerrainSurface.normal_smoothing_sigma_cells);
    }

    TEST(TerrainSurfaceCoverage, PresetDeterminism)
    {
        // Same field + same preset → identical cloud (bit-exact).
        const auto desc  = make_preset_desc();
        const auto field = make_ridge();
        const auto c1 = make_terrain_surface_splat_cloud(desc, field);
        const auto c2 = make_terrain_surface_splat_cloud(desc, field);
        ASSERT_EQ(c1.splats.size(), c2.splats.size());

        for (size_t i = 0; i < c1.splats.size(); ++i)
        {
            SCOPED_TRACE(i);
            const auto& a = c1.splats[i];
            const auto& b = c2.splats[i];
            EXPECT_EQ(a.position[0], b.position[0]);
            EXPECT_EQ(a.position[1], b.position[1]);
            EXPECT_EQ(a.position[2], b.position[2]);
            EXPECT_EQ(a.rotation[0], b.rotation[0]);
            EXPECT_EQ(a.rotation[1], b.rotation[1]);
            EXPECT_EQ(a.rotation[2], b.rotation[2]);
            EXPECT_EQ(a.rotation[3], b.rotation[3]);
            EXPECT_EQ(a.scale[0], b.scale[0]);
            EXPECT_EQ(a.scale[1], b.scale[1]);
            EXPECT_EQ(a.scale[2], b.scale[2]);
        }
    }

}  // namespace wz::engine::assets::test
