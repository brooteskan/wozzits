// tests/asset_gaussian_splat/terrain_surface_frame_handedness.cpp
//
// Regression tests for the terrain surface compiler's tangent frame
// construction.  These verify that the per-splat quaternion encodes a
// proper right-handed rotation (determinant +1) whose Y axis matches
// the expected surface normal.
//
// History: the original frame used cross(N, X) instead of cross(X, N),
// producing a left-handed (determinant -1) frame.  The matrix-to-
// quaternion conversion silently produced garbage — on pure Z-slopes
// the quaternion collapsed to identity, leaving discs flat and forming
// visible terraces on slopes.

#include <gtest/gtest.h>

#include <engine/assets/gaussian_splat/gaussian_splat.h>
#include <engine/assets/gaussian_splat/gaussian_splat_compilers.h>
#include <engine/assets/scalar_field/scalar_field.h>

#include <cmath>
#include <vector>

namespace wz::engine::assets::test
{
    namespace
    {
        struct Vec3 { float x, y, z; };

        // Recover the 3x3 rotation matrix columns from a PLY-convention
        // quaternion stored as (w, x, y, z) in rotation[0..3].
        struct Mat3
        {
            Vec3 col0, col1, col2; // columns: X, Y, Z of the rotated frame
        };

        Mat3 quat_to_matrix(const float r[4])
        {
            const float w = r[0], x = r[1], y = r[2], z = r[3];

            // Column 0: where local X goes.
            const Vec3 c0{
                1.0f - 2.0f * (y * y + z * z),
                2.0f * (x * y + w * z),
                2.0f * (x * z - w * y),
            };
            // Column 1: where local Y goes (= surface normal axis).
            const Vec3 c1{
                2.0f * (x * y - w * z),
                1.0f - 2.0f * (x * x + z * z),
                2.0f * (y * z + w * x),
            };
            // Column 2: where local Z goes.
            const Vec3 c2{
                2.0f * (x * z + w * y),
                2.0f * (y * z - w * x),
                1.0f - 2.0f * (x * x + y * y),
            };
            return { c0, c1, c2 };
        }

        // Determinant of a 3x3 matrix given by its columns.
        float determinant(const Mat3& m)
        {
            // det = c0 . (c1 x c2)
            const Vec3 cross{
                m.col1.y * m.col2.z - m.col1.z * m.col2.y,
                m.col1.z * m.col2.x - m.col1.x * m.col2.z,
                m.col1.x * m.col2.y - m.col1.y * m.col2.x,
            };
            return m.col0.x * cross.x
                 + m.col0.y * cross.y
                 + m.col0.z * cross.z;
        }

        // dot(cross(X, Y), Z) — positive means right-handed.
        float frame_handedness(const Mat3& m)
        {
            const Vec3 xy_cross{
                m.col0.y * m.col1.z - m.col0.z * m.col1.y,
                m.col0.z * m.col1.x - m.col0.x * m.col1.z,
                m.col0.x * m.col1.y - m.col0.y * m.col1.x,
            };
            return xy_cross.x * m.col2.x
                 + xy_cross.y * m.col2.y
                 + xy_cross.z * m.col2.z;
        }

        float dot(Vec3 a, Vec3 b)
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        float length(Vec3 a)
        {
            return std::sqrt(dot(a, a));
        }

        Vec3 normalize(Vec3 a)
        {
            const float len = length(a);
            if (len < 1e-12f) return { 0.0f, 1.0f, 0.0f };
            return { a.x / len, a.y / len, a.z / len };
        }

        // ─── Synthetic heightfields ──────────────────────────────────────

        // Flat 8x8 field at height 0.
        ScalarFieldData make_flat_field()
        {
            ScalarFieldData f{};
            f.width = 8; f.height = 8; f.depth = 1;
            f.format = ScalarFieldFormat::Float32;
            f.domain_kind = ScalarFieldDomainKind::Spatial2D;
            f.values.assign(64, 0.0f);
            f.min_value = 0.0f;
            f.max_value = 0.0f;
            return f;
        }

        // Constant ramp in +X direction: h(ix, iz) = ix * 0.5.
        ScalarFieldData make_x_ramp_field()
        {
            ScalarFieldData f{};
            f.width = 16; f.height = 16; f.depth = 1;
            f.format = ScalarFieldFormat::Float32;
            f.domain_kind = ScalarFieldDomainKind::Spatial2D;
            f.values.resize(256);
            for (uint32_t iz = 0; iz < 16; ++iz)
                for (uint32_t ix = 0; ix < 16; ++ix)
                    f.values[ix + iz * 16] = static_cast<float>(ix) * 0.5f;
            f.min_value = 0.0f;
            f.max_value = 7.5f;
            return f;
        }

        // Constant ramp in +Z direction: h(ix, iz) = iz * 0.5.
        ScalarFieldData make_z_ramp_field()
        {
            ScalarFieldData f{};
            f.width = 16; f.height = 16; f.depth = 1;
            f.format = ScalarFieldFormat::Float32;
            f.domain_kind = ScalarFieldDomainKind::Spatial2D;
            f.values.resize(256);
            for (uint32_t iz = 0; iz < 16; ++iz)
                for (uint32_t ix = 0; ix < 16; ++ix)
                    f.values[ix + iz * 16] = static_cast<float>(iz) * 0.5f;
            f.min_value = 0.0f;
            f.max_value = 7.5f;
            return f;
        }

        // Diagonal ramp: h(ix, iz) = (ix + iz) * 0.3.
        ScalarFieldData make_diagonal_ramp_field()
        {
            ScalarFieldData f{};
            f.width = 16; f.height = 16; f.depth = 1;
            f.format = ScalarFieldFormat::Float32;
            f.domain_kind = ScalarFieldDomainKind::Spatial2D;
            f.values.resize(256);
            float mn = 1e30f, mx = -1e30f;
            for (uint32_t iz = 0; iz < 16; ++iz)
                for (uint32_t ix = 0; ix < 16; ++ix)
                {
                    const float v =
                        static_cast<float>(ix + iz) * 0.3f;
                    f.values[ix + iz * 16] = v;
                    if (v < mn) mn = v;
                    if (v > mx) mx = v;
                }
            f.min_value = mn;
            f.max_value = mx;
            return f;
        }

        GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc
            make_default_desc()
        {
            GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc desc{};
            desc.step_x = 1.0f;
            desc.step_z = 1.0f;
            desc.height_scale = 1.0f;
            return desc;
        }
    }


    // ─── Flat field: quaternion is identity, frame is right-handed ──────

    TEST(TerrainSurfaceFrameHandedness, FlatFieldIsIdentityAndRightHanded)
    {
        const auto field = make_flat_field();
        const auto desc  = make_default_desc();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        ASSERT_TRUE(cloud.valid());

        for (const auto& s : cloud.splats)
        {
            const Mat3 m = quat_to_matrix(s.rotation);

            // Determinant must be +1 (right-handed, proper rotation).
            EXPECT_NEAR(determinant(m), 1.0f, 1e-3f);

            // dot(cross(X, Y), Z) > 0 — direct handedness check.
            EXPECT_GT(frame_handedness(m), 0.99f);

            // Normal (column 1) should be world Y.
            EXPECT_NEAR(m.col1.x, 0.0f, 1e-3f);
            EXPECT_NEAR(m.col1.y, 1.0f, 1e-3f);
            EXPECT_NEAR(m.col1.z, 0.0f, 1e-3f);
        }
    }


    // ─── X-ramp: quaternion is NOT identity, normal tilts toward -X ─────

    TEST(TerrainSurfaceFrameHandedness, XRampQuatIsNotIdentity)
    {
        const auto field = make_x_ramp_field();
        const auto desc  = make_default_desc();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        ASSERT_TRUE(cloud.valid());

        // Inspect an interior splat far from boundaries (stable central
        // differences).  Row 8, column 8 in a 16x16 grid.
        const auto& s = cloud.splats[8 + 8 * 16];

        // The quaternion must NOT be identity on a slope.
        const bool is_identity =
            std::abs(s.rotation[0] - 1.0f) < 1e-4f
            && std::abs(s.rotation[1]) < 1e-4f
            && std::abs(s.rotation[2]) < 1e-4f
            && std::abs(s.rotation[3]) < 1e-4f;
        EXPECT_FALSE(is_identity)
            << "quaternion collapsed to identity on an X-slope — "
               "frame handedness bug";

        const Mat3 m = quat_to_matrix(s.rotation);

        // Determinant must be +1.
        EXPECT_NEAR(determinant(m), 1.0f, 1e-3f);
        EXPECT_GT(frame_handedness(m), 0.99f);

        // Expected analytic normal for dh/du = 0.5, step_x = 1:
        //   Tu = (1, 0.5, 0), Tv = (0, 0, 1)
        //   N_raw = cross(Tv, Tu) = (-0.5, 1, 0)
        //   N_hat = normalize(-0.5, 1, 0) ≈ (-0.447, 0.894, 0)
        const Vec3 expected_normal = normalize({ -0.5f, 1.0f, 0.0f });
        EXPECT_NEAR(m.col1.x, expected_normal.x, 0.02f);
        EXPECT_NEAR(m.col1.y, expected_normal.y, 0.02f);
        EXPECT_NEAR(m.col1.z, expected_normal.z, 0.02f);
    }


    // ─── Z-ramp: same checks — this is the axis that collapsed to
    //     identity with the old left-handed bug ────────────────────────

    TEST(TerrainSurfaceFrameHandedness, ZRampQuatIsNotIdentity)
    {
        const auto field = make_z_ramp_field();
        const auto desc  = make_default_desc();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        ASSERT_TRUE(cloud.valid());

        const auto& s = cloud.splats[8 + 8 * 16];

        const bool is_identity =
            std::abs(s.rotation[0] - 1.0f) < 1e-4f
            && std::abs(s.rotation[1]) < 1e-4f
            && std::abs(s.rotation[2]) < 1e-4f
            && std::abs(s.rotation[3]) < 1e-4f;
        EXPECT_FALSE(is_identity)
            << "quaternion collapsed to identity on a Z-slope — "
               "frame handedness bug (this was the original failure mode)";

        const Mat3 m = quat_to_matrix(s.rotation);

        EXPECT_NEAR(determinant(m), 1.0f, 1e-3f);
        EXPECT_GT(frame_handedness(m), 0.99f);

        // Expected: dh/dv = 0.5, step_z = 1.
        //   Tu = (1, 0, 0), Tv = (0, 0.5, 1)
        //   N_raw = cross(Tv, Tu) = (0, 1, -0.5)
        //   N_hat ≈ (0, 0.894, -0.447)
        const Vec3 expected_normal = normalize({ 0.0f, 1.0f, -0.5f });
        EXPECT_NEAR(m.col1.x, expected_normal.x, 0.02f);
        EXPECT_NEAR(m.col1.y, expected_normal.y, 0.02f);
        EXPECT_NEAR(m.col1.z, expected_normal.z, 0.02f);
    }


    // ─── Diagonal ramp: frame is right-handed even on combined slopes ──

    TEST(TerrainSurfaceFrameHandedness, DiagonalRampIsRightHanded)
    {
        const auto field = make_diagonal_ramp_field();
        const auto desc  = make_default_desc();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        ASSERT_TRUE(cloud.valid());

        // Check ALL interior splats (skip 1-cell boundary for stable
        // central differences).
        for (uint32_t iz = 1; iz < 15; ++iz)
        {
            for (uint32_t ix = 1; ix < 15; ++ix)
            {
                const auto& s = cloud.splats[ix + iz * 16];
                const Mat3 m  = quat_to_matrix(s.rotation);

                EXPECT_NEAR(determinant(m), 1.0f, 1e-3f)
                    << "at (" << ix << ", " << iz << ")";
                EXPECT_GT(frame_handedness(m), 0.99f)
                    << "at (" << ix << ", " << iz << ")";
            }
        }
    }


    // ─── X-ramp with normal smoothing: same handedness guarantees ──────

    TEST(TerrainSurfaceFrameHandedness, SmoothedXRampIsRightHanded)
    {
        const auto field = make_x_ramp_field();
        auto desc = make_default_desc();
        desc.normal_smoothing_enabled = true;
        desc.normal_smoothing_radius_cells = 3;
        desc.normal_smoothing_sigma_cells = 1.5f;

        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        ASSERT_TRUE(cloud.valid());

        const auto& s = cloud.splats[8 + 8 * 16];
        const Mat3 m  = quat_to_matrix(s.rotation);

        EXPECT_NEAR(determinant(m), 1.0f, 1e-3f);
        EXPECT_GT(frame_handedness(m), 0.99f);

        // Smoothing on a uniform ramp shouldn't change the normal.
        const Vec3 expected_normal = normalize({ -0.5f, 1.0f, 0.0f });
        EXPECT_NEAR(m.col1.x, expected_normal.x, 0.02f);
        EXPECT_NEAR(m.col1.y, expected_normal.y, 0.02f);
        EXPECT_NEAR(m.col1.z, expected_normal.z, 0.02f);
    }


    // ─── Frame X axis aligns with Tu direction (Mode B property) ───────

    TEST(TerrainSurfaceFrameHandedness, FrameXAlignedWithTangentU)
    {
        const auto field = make_x_ramp_field();
        const auto desc  = make_default_desc();
        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        ASSERT_TRUE(cloud.valid());

        const auto& s = cloud.splats[8 + 8 * 16];
        const Mat3 m  = quat_to_matrix(s.rotation);

        // Tu = (1, 0.5, 0), normalized ≈ (0.894, 0.447, 0).
        // Frame's X column (col0) should align with this.
        const Vec3 Tu_hat = normalize({ 1.0f, 0.5f, 0.0f });
        const float alignment = std::abs(dot(m.col0, Tu_hat));
        EXPECT_GT(alignment, 0.99f)
            << "frame X axis should be aligned with the U tangent direction";
    }
}
