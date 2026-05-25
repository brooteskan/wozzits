// tests/asset_gaussian_splat/terrain_reconstruction.cpp
//
// Tests for the terrain grid reconstruction algorithm
// (reconstruct_terrain_grid).  Uses synthetic splat configurations
// with known expected results.

#include <gtest/gtest.h>

#include <engine/assets/gaussian_splat/terrain_reconstruction.h>
#include <engine/assets/gaussian_splat/terrain_coverage_kernel.h>

#include <cmath>
#include <vector>

namespace wz::engine::assets::test
{
    namespace
    {
        // Build a single flat splat at the origin with given height and
        // color.  radius_u = radius_v = radius.  Normal points up.
        TerrainReconSplat make_flat_splat(
            float x, float z, float height,
            float radius, float r, float g, float b,
            float opacity = 1.0f)
        {
            TerrainReconSplat s{};
            s.pos_x    = x;
            s.pos_z    = z;
            s.height   = height;
            s.radius_u = radius;
            s.radius_v = radius;
            s.normal[0] = 0.0f;
            s.normal[1] = 1.0f;
            s.normal[2] = 0.0f;
            s.color[0] = r;
            s.color[1] = g;
            s.color[2] = b;
            s.opacity  = opacity;
            return s;
        }

        TerrainReconDesc make_default_desc(int grid_res = 32)
        {
            TerrainReconDesc desc{};
            desc.grid_res = grid_res;
            desc.kernel_mode = TerrainCoverageKernelMode::HardDisc;
            desc.radius_scale = 1.0f;
            desc.use_gradient_normals = true;
            return desc;
        }
    }


    // ─── Empty / degenerate input ────────────────────────────────────

    TEST(TerrainReconstruction, EmptyInputReturnsInvalid)
    {
        std::vector<TerrainReconSplat> empty;
        auto desc = make_default_desc();
        auto result = reconstruct_terrain_grid(empty, desc);
        EXPECT_FALSE(result.valid());
        EXPECT_TRUE(result.vertices.empty());
        EXPECT_TRUE(result.indices.empty());
    }

    TEST(TerrainReconstruction, GridResTooSmallReturnsInvalid)
    {
        std::vector<TerrainReconSplat> splats;
        splats.push_back(make_flat_splat(0.0f, 0.0f, 5.0f, 1.0f, 0.5f, 0.5f, 0.5f));

        auto desc = make_default_desc(1); // grid_res = 1, need >= 2
        desc.domain_min_x = -1.0f; desc.domain_max_x = 1.0f;
        desc.domain_min_z = -1.0f; desc.domain_max_z = 1.0f;
        auto result = reconstruct_terrain_grid(splats, desc);
        EXPECT_FALSE(result.valid());
    }


    // ─── Flat splat field ────────────────────────────────────────────

    TEST(TerrainReconstruction, FlatSplatsReconstructFlatGrid)
    {
        // Place a grid of flat splats at height = 5.0 covering the domain.
        std::vector<TerrainReconSplat> splats;
        const float h = 5.0f;
        const float r = 2.0f;
        for (float x = -4.0f; x <= 4.0f; x += 2.0f)
            for (float z = -4.0f; z <= 4.0f; z += 2.0f)
                splats.push_back(make_flat_splat(x, z, h, r, 0.5f, 0.5f, 0.5f));

        auto desc = make_default_desc(16);
        desc.domain_min_x = -4.0f; desc.domain_max_x = 4.0f;
        desc.domain_min_z = -4.0f; desc.domain_max_z = 4.0f;
        // SmoothDisc for broad coverage
        desc.kernel_mode = TerrainCoverageKernelMode::HardDisc;

        auto result = reconstruct_terrain_grid(splats, desc);
        ASSERT_TRUE(result.valid());

        // Interior vertices (away from domain edge) should be at height ~5.
        // Check a central subset.
        int flat_count = 0;
        for (int iz = 4; iz < 12; ++iz)
        {
            for (int ix = 4; ix < 12; ++ix)
            {
                const auto& v = result.vertices[iz * 16 + ix];
                if (v.weight > 0.01f) // only check covered cells
                {
                    EXPECT_NEAR(v.pos[1], h, 0.1f)
                        << "at grid (" << ix << ", " << iz << ")";
                    ++flat_count;
                }
            }
        }
        EXPECT_GT(flat_count, 0) << "expected at least some covered interior cells";
    }


    // ─── Single color reconstruction ─────────────────────────────────

    TEST(TerrainReconstruction, SingleColorReconstructsExpectedColor)
    {
        std::vector<TerrainReconSplat> splats;
        // Fill with red splats.
        for (float x = -2.0f; x <= 2.0f; x += 1.0f)
            for (float z = -2.0f; z <= 2.0f; z += 1.0f)
                splats.push_back(make_flat_splat(x, z, 0.0f, 2.0f, 1.0f, 0.0f, 0.25f));

        auto desc = make_default_desc(8);
        desc.domain_min_x = -2.0f; desc.domain_max_x = 2.0f;
        desc.domain_min_z = -2.0f; desc.domain_max_z = 2.0f;
        desc.kernel_mode = TerrainCoverageKernelMode::HardDisc;

        auto result = reconstruct_terrain_grid(splats, desc);
        ASSERT_TRUE(result.valid());

        // Interior covered cells should be (1, 0, 0.25).
        for (int iz = 2; iz < 6; ++iz)
        {
            for (int ix = 2; ix < 6; ++ix)
            {
                const auto& v = result.vertices[iz * 8 + ix];
                if (v.weight > 0.01f)
                {
                    EXPECT_NEAR(v.color[0], 1.0f,  1e-3f);
                    EXPECT_NEAR(v.color[1], 0.0f,  1e-3f);
                    EXPECT_NEAR(v.color[2], 0.25f, 1e-3f);
                }
            }
        }
    }


    // ─── Two-splat blend ─────────────────────────────────────────────

    TEST(TerrainReconstruction, TwoSplatBlendGivesMidpoint)
    {
        // Two splats at same position, different heights and colors.
        // With equal opacity and radius, their blend should give the midpoint.
        std::vector<TerrainReconSplat> splats;
        splats.push_back(make_flat_splat(0.0f, 0.0f, 0.0f, 5.0f, 1.0f, 0.0f, 0.0f));
        splats.push_back(make_flat_splat(0.0f, 0.0f, 10.0f, 5.0f, 0.0f, 1.0f, 0.0f));

        auto desc = make_default_desc(8);
        desc.domain_min_x = -1.0f; desc.domain_max_x = 1.0f;
        desc.domain_min_z = -1.0f; desc.domain_max_z = 1.0f;
        desc.kernel_mode = TerrainCoverageKernelMode::HardDisc;

        auto result = reconstruct_terrain_grid(splats, desc);
        ASSERT_TRUE(result.valid());

        // Center vertex (4,4) in an 8×8 grid, at position (0,0) — right at
        // both splat centers.
        // Note: with N=8 vertices, center is between (3,3) and (4,4).
        // Use (3,3) or (4,4) — both are close to center.
        const auto& v = result.vertices[3 * 8 + 3];
        EXPECT_NEAR(v.pos[1], 5.0f, 0.1f) << "height should be midpoint of 0 and 10";
        EXPECT_NEAR(v.color[0], 0.5f, 0.01f) << "red channel should be midpoint";
        EXPECT_NEAR(v.color[1], 0.5f, 0.01f) << "green channel should be midpoint";
        EXPECT_NEAR(v.color[2], 0.0f, 0.01f) << "blue should be 0 (both have 0)";
    }


    // ─── Grid topology ───────────────────────────────────────────────

    TEST(TerrainReconstruction, CorrectVertexAndIndexCount)
    {
        std::vector<TerrainReconSplat> splats;
        splats.push_back(make_flat_splat(0.0f, 0.0f, 1.0f, 10.0f, 0.5f, 0.5f, 0.5f));

        const int N = 16;
        auto desc = make_default_desc(N);
        desc.domain_min_x = -5.0f; desc.domain_max_x = 5.0f;
        desc.domain_min_z = -5.0f; desc.domain_max_z = 5.0f;

        auto result = reconstruct_terrain_grid(splats, desc);
        ASSERT_TRUE(result.valid());

        EXPECT_EQ(result.vertices.size(), static_cast<size_t>(N * N));
        // (N-1) × (N-1) cells, 2 triangles each, 3 indices per triangle.
        EXPECT_EQ(result.indices.size(),
            static_cast<size_t>((N - 1) * (N - 1) * 6));
    }


    // ─── Gradient normals on a ramp ──────────────────────────────────

    TEST(TerrainReconstruction, GradientNormalsOnXRamp)
    {
        // Splats forming a ramp: height = x.  The gradient normal should
        // tilt toward -X: N ≈ normalize(-1, 1, 0).
        std::vector<TerrainReconSplat> splats;
        for (float x = -4.0f; x <= 4.0f; x += 0.5f)
            for (float z = -4.0f; z <= 4.0f; z += 0.5f)
                splats.push_back(make_flat_splat(x, z, x, 1.5f, 0.5f, 0.5f, 0.5f));

        auto desc = make_default_desc(32);
        desc.domain_min_x = -4.0f; desc.domain_max_x = 4.0f;
        desc.domain_min_z = -4.0f; desc.domain_max_z = 4.0f;
        desc.kernel_mode = TerrainCoverageKernelMode::HardDisc;
        desc.use_gradient_normals = true;

        auto result = reconstruct_terrain_grid(splats, desc);
        ASSERT_TRUE(result.valid());

        // Sample an interior vertex.
        const auto& v = result.vertices[16 * 32 + 16]; // center
        if (v.weight > 0.01f)
        {
            // Normal should have negative X component and positive Y.
            EXPECT_LT(v.normal[0], 0.0f) << "normal X should be negative on +X ramp";
            EXPECT_GT(v.normal[1], 0.0f) << "normal Y should be positive";
            EXPECT_NEAR(v.normal[2], 0.0f, 0.1f) << "normal Z should be ~0 (no Z slope)";
        }
    }


    // ─── Auto domain bounds ──────────────────────────────────────────

    TEST(TerrainReconstruction, AutoDomainBoundsFromSplats)
    {
        std::vector<TerrainReconSplat> splats;
        splats.push_back(make_flat_splat(-3.0f, -2.0f, 1.0f, 5.0f, 0.5f, 0.5f, 0.5f));
        splats.push_back(make_flat_splat( 3.0f,  2.0f, 1.0f, 5.0f, 0.5f, 0.5f, 0.5f));

        auto desc = make_default_desc(8);
        // Leave domain at 0,0,0,0 → auto-compute from splats.

        auto result = reconstruct_terrain_grid(splats, desc);
        ASSERT_TRUE(result.valid());

        // Vertices should span from (-3, -2) to (3, 2) in XZ.
        const auto& corner_min = result.vertices[0];
        const auto& corner_max = result.vertices[7 * 8 + 7];
        EXPECT_NEAR(corner_min.pos[0], -3.0f, 0.01f);
        EXPECT_NEAR(corner_min.pos[2], -2.0f, 0.01f);
        EXPECT_NEAR(corner_max.pos[0],  3.0f, 0.01f);
        EXPECT_NEAR(corner_max.pos[2],  2.0f, 0.01f);
    }


    // ─── Height range in result ──────────────────────────────────────

    TEST(TerrainReconstruction, HeightMinMaxTracked)
    {
        // Two well-separated splats at different heights with small radii
        // so they don't overlap.  The reconstructed min/max should reflect
        // each splat's height individually.
        std::vector<TerrainReconSplat> splats;
        splats.push_back(make_flat_splat(-5.0f, 0.0f, -3.0f, 1.0f, 0.5f, 0.5f, 0.5f));
        splats.push_back(make_flat_splat( 5.0f, 0.0f, 12.0f, 1.0f, 0.5f, 0.5f, 0.5f));

        auto desc = make_default_desc(32);
        desc.domain_min_x = -6.0f; desc.domain_max_x = 6.0f;
        desc.domain_min_z = -1.0f; desc.domain_max_z = 1.0f;
        desc.kernel_mode = TerrainCoverageKernelMode::HardDisc;

        auto result = reconstruct_terrain_grid(splats, desc);
        ASSERT_TRUE(result.valid());

        // Min should be at or below the low splat's height, max at or above
        // the high splat's height.  Uncovered cells default to 0, so
        // height_min may be 0.  Check that max reaches near the high splat.
        EXPECT_LE(result.height_min, 0.0f);
        EXPECT_GE(result.height_max, 11.0f);
    }

}  // namespace
