// tests/asset_gaussian_splat/terrain_normal_smoothing.cpp
//
// Direct tests against make_terrain_surface_splat_cloud's normal-smoothing
// behaviour.  Builds small synthetic heightmaps with known features and
// inspects the per-splat tangent frame.
//
// The splat's "world normal" is rotate_by_quat(local_Y, splat_rotation),
// which equals the second row of the rotation matrix the compiler built
// from its frame.  We reconstruct it from the stored (w,x,y,z) quaternion
// and compare across configurations.

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

        // Apply the quaternion (stored as w, x, y, z) to local Y axis to
        // recover the world-space surface normal.
        Vec3 quat_to_world_normal(const float r[4])
        {
            const float qw = r[0];
            const float qx = r[1];
            const float qy = r[2];
            const float qz = r[3];
            // local Y = (0, 1, 0). Rotated by q gives:
            //   2*(x*y - w*z),  1 - 2*(x*x + z*z),  2*(y*z + w*x)
            return Vec3{
                2.0f * (qx * qy - qw * qz),
                1.0f - 2.0f * (qx * qx + qz * qz),
                2.0f * (qy * qz + qw * qx),
            };
        }

        // Build a flat 8x8 field at height 0.
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

        // Build an 8x8 field with a single high-amplitude noise spike at
        // the centre — produces a sharp normal discontinuity that
        // smoothing should attenuate.
        ScalarFieldData make_spike_field()
        {
            ScalarFieldData f{};
            f.width = 8; f.height = 8; f.depth = 1;
            f.format = ScalarFieldFormat::Float32;
            f.domain_kind = ScalarFieldDomainKind::Spatial2D;
            f.values.assign(64, 0.0f);
            // Tall narrow spike at (3,3).
            f.values[3 + 3 * 8] = 5.0f;
            f.min_value = 0.0f;
            f.max_value = 5.0f;
            return f;
        }

        // Stable ramp in +X direction so the analytic normal is known
        // (sloping into world +Y but tilted toward -X).
        ScalarFieldData make_ramp_field()
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
            f.max_value = 15.0f * 0.5f;
            return f;
        }
    }


    // ─── Flat field: smoothed normals should still point straight up ──

    TEST(TerrainNormalSmoothing, FlatFieldStaysUp)
    {
        const auto field = make_flat_field();

        GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc desc{};
        desc.step_x = 1.0f;
        desc.step_z = 1.0f;
        desc.height_scale = 1.0f;
        desc.normal_smoothing_enabled = true;
        desc.normal_smoothing_radius_cells = 2;
        desc.normal_smoothing_sigma_cells = 1.0f;

        const auto cloud = make_terrain_surface_splat_cloud(desc, field);
        ASSERT_TRUE(cloud.valid());

        for (const auto& s : cloud.splats)
        {
            const Vec3 n = quat_to_world_normal(s.rotation);
            EXPECT_NEAR(n.x, 0.0f, 1e-3f);
            EXPECT_NEAR(n.y, 1.0f, 1e-3f);
            EXPECT_NEAR(n.z, 0.0f, 1e-3f);
        }
    }


    // ─── Spike field: smoothing should attenuate the spike's effect ──

    TEST(TerrainNormalSmoothing, SpikeFieldSmoothingReducesNormalSpread)
    {
        const auto field = make_spike_field();

        // Common base desc.
        GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc base{};
        base.step_x = 1.0f;
        base.step_z = 1.0f;
        base.height_scale = 1.0f;

        // Without smoothing.
        auto raw_desc = base;
        raw_desc.normal_smoothing_enabled = false;
        const auto raw = make_terrain_surface_splat_cloud(raw_desc, field);

        // With smoothing.
        auto smooth_desc = base;
        smooth_desc.normal_smoothing_enabled = true;
        smooth_desc.normal_smoothing_radius_cells = 2;
        smooth_desc.normal_smoothing_sigma_cells  = 1.0f;
        const auto smoothed = make_terrain_surface_splat_cloud(smooth_desc, field);

        ASSERT_EQ(raw.splat_count(), smoothed.splat_count());

        // Compare adjacent-normal "delta" magnitude on the row crossing
        // the spike (z = 3).  Smoothed should be strictly smaller.
        const uint32_t W = field.width;
        const uint32_t row = 3;

        auto sample_at = [&](const GaussianSplatCloudData& cloud, uint32_t ix) -> Vec3 {
            // splats are emitted in row-major order at subsample_step=1.
            const auto& s = cloud.splats[ix + row * W];
            return quat_to_world_normal(s.rotation);
        };

        float raw_delta = 0.0f, smooth_delta = 0.0f;
        for (uint32_t ix = 1; ix < W; ++ix)
        {
            const Vec3 ra = sample_at(raw,      ix - 1);
            const Vec3 rb = sample_at(raw,      ix);
            const Vec3 sa = sample_at(smoothed, ix - 1);
            const Vec3 sb = sample_at(smoothed, ix);
            const float drx = rb.x - ra.x;
            const float dry = rb.y - ra.y;
            const float drz = rb.z - ra.z;
            const float dsx = sb.x - sa.x;
            const float dsy = sb.y - sa.y;
            const float dsz = sb.z - sa.z;
            raw_delta    += std::sqrt(drx*drx + dry*dry + drz*drz);
            smooth_delta += std::sqrt(dsx*dsx + dsy*dsy + dsz*dsz);
        }

        EXPECT_LT(smooth_delta, raw_delta)
            << "smoothed total adjacent-normal delta ("
            << smooth_delta
            << ") should be smaller than raw delta ("
            << raw_delta << ")";
    }


    // ─── Constant ramp: normal should be the same everywhere, with or
    //     without smoothing (no discontinuity to smooth) ──

    TEST(TerrainNormalSmoothing, RampHasUniformNormal)
    {
        const auto field = make_ramp_field();

        // Without smoothing.
        GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc desc{};
        desc.step_x = 1.0f;
        desc.step_z = 1.0f;
        desc.height_scale = 1.0f;

        const auto raw = make_terrain_surface_splat_cloud(desc, field);

        // With smoothing — same field, smoothing should be a no-op.
        desc.normal_smoothing_enabled = true;
        desc.normal_smoothing_radius_cells = 3;
        desc.normal_smoothing_sigma_cells = 1.5f;
        const auto smoothed = make_terrain_surface_splat_cloud(desc, field);

        ASSERT_EQ(raw.splat_count(), smoothed.splat_count());

        // Inspect an interior splat away from the boundary; smoothing
        // should agree with raw to high precision because the underlying
        // normal field is uniform.
        const uint32_t W = field.width;
        const uint32_t row = 8, col = 8;
        const auto& r = raw     .splats[col + row * W];
        const auto& s = smoothed.splats[col + row * W];
        const Vec3 nr = quat_to_world_normal(r.rotation);
        const Vec3 ns = quat_to_world_normal(s.rotation);
        EXPECT_NEAR(nr.x, ns.x, 1e-3f);
        EXPECT_NEAR(nr.y, ns.y, 1e-3f);
        EXPECT_NEAR(nr.z, ns.z, 1e-3f);
    }


    // ─── Determinism: identical (desc, field) → identical output ──

    TEST(TerrainNormalSmoothing, IsDeterministic)
    {
        const auto field = make_spike_field();

        GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc desc{};
        desc.step_x = 1.0f;
        desc.step_z = 1.0f;
        desc.height_scale = 1.0f;
        desc.normal_smoothing_enabled = true;
        desc.normal_smoothing_radius_cells = 2;
        desc.normal_smoothing_sigma_cells = 1.0f;

        const auto a = make_terrain_surface_splat_cloud(desc, field);
        const auto b = make_terrain_surface_splat_cloud(desc, field);
        ASSERT_EQ(a.splat_count(), b.splat_count());
        for (size_t i = 0; i < a.splats.size(); ++i)
        {
            for (int j = 0; j < 4; ++j)
                EXPECT_FLOAT_EQ(a.splats[i].rotation[j], b.splats[i].rotation[j]);
        }
    }
}
