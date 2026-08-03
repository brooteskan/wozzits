#include <engine/collision/collision_surface_sampling.h>

#include <engine/assets/scalar_field/scalar_field_compilers.h>

#include <cmath>
#include <vector>

#include <support/fp_expectations.h>

#include <gtest/gtest.h>

namespace
{
    wz::engine::assets::CollisionAssetData terrain_mesh_surface_with_triangle_in_cell(
        uint32_t cell_with_triangle)
    {
        wz::engine::assets::CollisionAssetData surface{};
        surface.shape_kind =
            wz::engine::assets::CollisionShapeKind::TerrainMeshSurface;
        surface.occupancy.queryable = true;
        surface.bounds_min[0] = 0.0f;
        surface.bounds_min[1] = 5.0f;
        surface.bounds_min[2] = 0.0f;
        surface.bounds_max[0] = 2.0f;
        surface.bounds_max[1] = 5.0f;
        surface.bounds_max[2] = 2.0f;
        surface.points = {
            wz::engine::assets::CollisionPoint{
                .position = { 0.25f, 5.0f, 0.25f },
            },
            wz::engine::assets::CollisionPoint{
                .position = { 1.75f, 5.0f, 0.25f },
            },
            wz::engine::assets::CollisionPoint{
                .position = { 0.25f, 5.0f, 1.75f },
            },
        };
        surface.indices = { 0u, 1u, 2u };

        auto& grid = surface.surface_grid;
        grid.origin_x = 0.0f;
        grid.origin_z = 0.0f;
        grid.cell_size_x = 1.0f;
        grid.cell_size_z = 1.0f;
        grid.cells_x = 2u;
        grid.cells_z = 2u;
        grid.cell_bounds.resize(4u);
        grid.cell_offsets.resize(5u, 0u);
        grid.cell_triangle_indices = { 0u };
        for (uint32_t cell = 0; cell < 4u; ++cell) {
            grid.cell_offsets[cell + 1u] =
                cell < cell_with_triangle ? 0u : 1u;
        }

        return surface;
    }

    wz::engine::assets::CollisionAssetData sloped_terrain_mesh_surface()
    {
        auto surface = terrain_mesh_surface_with_triangle_in_cell(0u);
        surface.points[0].position[1] = 4.0f;
        surface.points[1].position[1] = 6.0f;
        surface.points[2].position[1] = 4.0f;
        surface.bounds_min[1] = 4.0f;
        surface.bounds_max[1] = 6.0f;
        return surface;
    }

    wz::engine::assets::CollisionAssetData quadratic_height_field_surface()
    {
        wz::engine::assets::CollisionAssetData surface{};
        surface.shape_kind =
            wz::engine::assets::CollisionShapeKind::TerrainHeightField;
        surface.occupancy.queryable = true;
        surface.bounds_min[0] = 0.0f;
        surface.bounds_min[1] = 0.0f;
        surface.bounds_min[2] = 0.0f;
        surface.bounds_max[0] = 2.0f;
        surface.bounds_max[1] = 4.0f;
        surface.bounds_max[2] = 1.0f;
        surface.origin[0] = 0.0f;
        surface.origin[1] = 0.0f;
        surface.size[0] = 2.0f;
        surface.size[1] = 1.0f;
        surface.resolution_x = 3u;
        surface.resolution_y = 2u;
        surface.vertical_scale = 1.0f;
        surface.height_samples = {
            0.0f, 1.0f, 4.0f,
            0.0f, 1.0f, 4.0f,
        };
        return surface;
    }

    wz::engine::collision::CollisionWorldEntry surface_entry(
        const wz::engine::assets::CollisionAssetData& surface,
        const wz::math::Mat4& world_from_local =
            wz::math::Mat4::identity())
    {
        return wz::engine::collision::CollisionWorldEntry{
            .entity = 7u,
            .world_from_local = world_from_local,
            .enabled = true,
            .resolved = &surface,
        };
    }
}

TEST(CollisionSurfaceSampling, MeshSurfaceSearchesNeighborCellsForExactHit)
{
    const auto surface = terrain_mesh_surface_with_triangle_in_cell(1u);
    wz::engine::collision::CollisionSurfaceSample sample{};

    ASSERT_TRUE(wz::engine::collision::sample_terrain_surface(
        surface_entry(surface),
        0.75f,
        0.75f,
        sample));

    EXPECT_TRUE(sample.hit);
    EXPECT_EQ(sample.surface_entity, 7u);
    EXPECT_NEAR(sample.position.x, 0.75f, 1e-5f);
    EXPECT_NEAR(sample.position.y, 5.0f, 1e-5f);
    EXPECT_NEAR(sample.position.z, 0.75f, 1e-5f);
    EXPECT_NEAR(sample.normal.y, 1.0f, 1e-5f);
}

TEST(CollisionSurfaceSampling, MeshSurfaceStillRejectsOutsideGrid)
{
    const auto surface = terrain_mesh_surface_with_triangle_in_cell(1u);
    wz::engine::collision::CollisionSurfaceSample sample{};

    EXPECT_FALSE(wz::engine::collision::sample_terrain_surface(
        surface_entry(surface),
        -0.25f,
        0.75f,
        sample));
    EXPECT_FALSE(sample.hit);
}

TEST(CollisionSurfaceSampling, NearestMeshSurfaceSamplesNearbyTriangle)
{
    const auto surface = sloped_terrain_mesh_surface();
    wz::engine::collision::CollisionSurfaceSample exact_sample{};
    ASSERT_FALSE(wz::engine::collision::sample_terrain_surface(
        surface_entry(surface),
        1.45f,
        1.45f,
        exact_sample));

    wz::engine::collision::CollisionSurfaceSample nearest_sample{};
    ASSERT_TRUE(wz::engine::collision::sample_nearest_terrain_surface(
        surface_entry(surface),
        1.45f,
        1.45f,
        nearest_sample));

    EXPECT_TRUE(nearest_sample.hit);
    EXPECT_EQ(nearest_sample.surface_entity, 7u);
    EXPECT_NEAR(nearest_sample.position.x, 1.45f, 1e-5f);
    EXPECT_NEAR(nearest_sample.position.y, 5.6f, 1e-5f);
    EXPECT_NEAR(nearest_sample.position.z, 1.45f, 1e-5f);
    EXPECT_NEAR(nearest_sample.normal.y, 0.6f, 1e-5f);
}

TEST(CollisionSurfaceSampling, MeshSurfaceSamplesThroughLargeWorldScale)
{
    const auto surface = terrain_mesh_surface_with_triangle_in_cell(0u);
    wz::math::Mat4 scaled = wz::math::Mat4::identity();
    scaled.m[0] = 1000.0f;
    scaled.m[5] = 1000.0f;
    scaled.m[10] = 1000.0f;

    wz::engine::collision::CollisionSurfaceSample sample{};
    ASSERT_TRUE(wz::engine::collision::sample_terrain_surface(
        surface_entry(surface, scaled),
        750.0f,
        750.0f,
        sample));

    EXPECT_TRUE(sample.hit);
    EXPECT_NEAR(sample.position.x, 750.0f, 1e-3f);
    EXPECT_NEAR(sample.position.y, 5000.0f, 1e-3f);
    EXPECT_NEAR(sample.position.z, 750.0f, 1e-3f);
    EXPECT_NEAR(sample.normal.y, 1.0f, 1e-5f);
}

TEST(CollisionSurfaceSampling, HeightFieldNormalDoesNotSnapAtHalfCell)
{
    const auto surface = quadratic_height_field_surface();
    wz::engine::collision::CollisionSurfaceSample left_sample{};
    wz::engine::collision::CollisionSurfaceSample right_sample{};

    ASSERT_TRUE(wz::engine::collision::sample_terrain_surface(
        surface_entry(surface),
        0.49f,
        0.4f,
        left_sample));
    ASSERT_TRUE(wz::engine::collision::sample_terrain_surface(
        surface_entry(surface),
        0.51f,
        0.4f,
        right_sample));

    EXPECT_NEAR(left_sample.normal.x, right_sample.normal.x, 0.03f);
    EXPECT_NEAR(left_sample.normal.y, right_sample.normal.y, 0.03f);
    EXPECT_NEAR(left_sample.normal.z, right_sample.normal.z, 0.03f);
}

TEST(CollisionSurfaceSampling, HeightFieldNormalIsSmoothAcrossCellBoundary)
{
    const auto surface = quadratic_height_field_surface();
    wz::engine::collision::CollisionSurfaceSample left_sample{};
    wz::engine::collision::CollisionSurfaceSample right_sample{};

    ASSERT_TRUE(wz::engine::collision::sample_terrain_surface(
        surface_entry(surface),
        0.99f,
        0.4f,
        left_sample));
    ASSERT_TRUE(wz::engine::collision::sample_terrain_surface(
        surface_entry(surface),
        1.01f,
        0.4f,
        right_sample));

    EXPECT_NEAR(left_sample.normal.x, right_sample.normal.x, 0.03f);
    EXPECT_NEAR(left_sample.normal.y, right_sample.normal.y, 0.03f);
    EXPECT_NEAR(left_sample.normal.z, right_sample.normal.z, 0.03f);
}

TEST(CollisionSurfaceSampling, HeightFieldExactSampleRejectsOffBounds)
{
    const auto surface = quadratic_height_field_surface();  // x extent [0, 2]
    wz::engine::collision::CollisionSurfaceSample sample{};
    // The exact sampler reports no surface past the field edge.
    EXPECT_FALSE(wz::engine::collision::sample_terrain_surface(
        surface_entry(surface), 3.0f, 0.5f, sample));
}

TEST(CollisionSurfaceSampling, HeightFieldNearestClampsOffBoundsToEdge)
{
    const auto surface = quadratic_height_field_surface();  // edge height 4 at x=2
    wz::engine::collision::CollisionSurfaceSample sample{};
    // The nearest-surface query clamps an off-field probe to the boundary, so an
    // actor that drove past the heightfield edge sticks to the rim height (4 at
    // x=2) instead of falling through.
    ASSERT_TRUE(wz::engine::collision::sample_nearest_terrain_surface(
        surface_entry(surface), 3.0f, 0.5f, sample));
    EXPECT_NEAR(sample.position.y, 4.0f, 1e-4f);
}

namespace
{
    wz::engine::assets::CollisionAssetData flat_height_field_surface(
        float height)
    {
        wz::engine::assets::CollisionAssetData surface{};
        surface.shape_kind =
            wz::engine::assets::CollisionShapeKind::TerrainHeightField;
        surface.occupancy.queryable = true;
        surface.origin[0] = 0.0f;
        surface.origin[1] = 0.0f;
        surface.size[0] = 2.0f;
        surface.size[1] = 2.0f;
        surface.resolution_x = 2u;
        surface.resolution_y = 2u;
        surface.vertical_scale = 1.0f;
        surface.base_height = 0.0f;
        surface.height_samples = { height, height, height, height };
        surface.min_height = height;
        surface.max_height = height;
        surface.bounds_min[0] = 0.0f;
        surface.bounds_min[1] = height;
        surface.bounds_min[2] = 0.0f;
        surface.bounds_max[0] = 2.0f;
        surface.bounds_max[1] = height;
        surface.bounds_max[2] = 2.0f;
        return surface;
    }

    // World height == local X: a linear ramp rising from 0 at x=0 to 10 at x=10,
    // flat in Z. A single 2x2 cell, so the bilinear surface is height(x, z) == x
    // exactly -- the closed form lets the ray-cast expectations be exact.
    wz::engine::assets::CollisionAssetData ramp_height_field_surface()
    {
        wz::engine::assets::CollisionAssetData surface{};
        surface.shape_kind =
            wz::engine::assets::CollisionShapeKind::TerrainHeightField;
        surface.occupancy.queryable = true;
        surface.origin[0] = 0.0f;
        surface.origin[1] = 0.0f;
        surface.size[0] = 10.0f;
        surface.size[1] = 10.0f;
        surface.resolution_x = 2u;
        surface.resolution_y = 2u;
        surface.vertical_scale = 1.0f;
        surface.base_height = 0.0f;
        surface.height_samples = { 0.0f, 10.0f, 0.0f, 10.0f };
        surface.min_height = 0.0f;
        surface.max_height = 10.0f;
        surface.bounds_min[0] = 0.0f;
        surface.bounds_min[1] = 0.0f;
        surface.bounds_min[2] = 0.0f;
        surface.bounds_max[0] = 10.0f;
        surface.bounds_max[1] = 10.0f;
        surface.bounds_max[2] = 10.0f;
        return surface;
    }
}

TEST(CollisionSurfaceSampling, HeightFieldRayHitsFlatSurfaceFromAbove)
{
    const auto surface = flat_height_field_surface(10.0f);
    wz::engine::collision::CollisionSurfaceSample sample{};
    ASSERT_TRUE(wz::engine::collision::raycast_terrain_surface(
        surface_entry(surface),
        wz::math::Vec3{ .x = 0.5f, .y = 20.0f, .z = 0.5f },
        wz::math::Vec3{ .x = 0.0f, .y = -1.0f, .z = 0.0f },
        100.0f,
        sample));
    EXPECT_TRUE(sample.hit);
    EXPECT_EQ(sample.surface_entity, 7u);
    EXPECT_NEAR(sample.position.x, 0.5f, 1e-3f);
    EXPECT_NEAR(sample.position.y, 10.0f, 1e-3f);
    EXPECT_NEAR(sample.position.z, 0.5f, 1e-3f);
    EXPECT_NEAR(sample.normal.y, 1.0f, 1e-4f);
}

TEST(CollisionSurfaceSampling, HeightFieldRayMissesWhenSkimmingAboveSurface)
{
    const auto surface = flat_height_field_surface(10.0f);
    wz::engine::collision::CollisionSurfaceSample sample{};
    // A horizontal ray at y=15 across a flat y=10 field stays above the surface
    // the whole way -- no crossing, so the shot flies over.
    EXPECT_FALSE(wz::engine::collision::raycast_terrain_surface(
        surface_entry(surface),
        wz::math::Vec3{ .x = 0.1f, .y = 15.0f, .z = 0.5f },
        wz::math::Vec3{ .x = 1.0f, .y = 0.0f, .z = 0.0f },
        100.0f,
        sample));
    EXPECT_FALSE(sample.hit);
}

TEST(CollisionSurfaceSampling, HeightFieldGrazingRayHitsSlopeWhereDrawn)
{
    const auto surface = ramp_height_field_surface();  // height == x
    wz::engine::collision::CollisionSurfaceSample sample{};
    // The regression: a low, near-horizontal shot at y=1 skims the shallow foot
    // of the ramp. It must strike exactly where the surface rises to meet it
    // (height == x, so x == 1), NOT punch through as the missing heightfield ray
    // path let it.
    ASSERT_TRUE(wz::engine::collision::raycast_terrain_surface(
        surface_entry(surface),
        wz::math::Vec3{ .x = 0.0f, .y = 1.0f, .z = 5.0f },
        wz::math::Vec3{ .x = 1.0f, .y = 0.0f, .z = 0.0f },
        20.0f,
        sample));
    EXPECT_TRUE(sample.hit);
    EXPECT_NEAR(sample.position.x, 1.0f, 2e-3f);
    EXPECT_NEAR(sample.position.y, 1.0f, 2e-3f);
    EXPECT_NEAR(sample.position.z, 5.0f, 2e-3f);
    EXPECT_GT(sample.normal.y, 0.0f);
}

TEST(CollisionSurfaceSampling, HeightFieldRayMissesWhenAboveEntireSlope)
{
    const auto surface = ramp_height_field_surface();  // max height 10
    wz::engine::collision::CollisionSurfaceSample sample{};
    // y=11 clears the whole ramp -> the shot flies over and never lands.
    EXPECT_FALSE(wz::engine::collision::raycast_terrain_surface(
        surface_entry(surface),
        wz::math::Vec3{ .x = 0.0f, .y = 11.0f, .z = 5.0f },
        wz::math::Vec3{ .x = 1.0f, .y = 0.0f, .z = 0.0f },
        20.0f,
        sample));
}

TEST(CollisionSurfaceSampling, HeightFieldRayRespectsMaxDistance)
{
    const auto surface = ramp_height_field_surface();  // crossing is 1 unit away
    wz::engine::collision::CollisionSurfaceSample sample{};
    // The surface crossing is 1 unit down the ray; a 0.5-unit reach stops short.
    EXPECT_FALSE(wz::engine::collision::raycast_terrain_surface(
        surface_entry(surface),
        wz::math::Vec3{ .x = 0.0f, .y = 1.0f, .z = 5.0f },
        wz::math::Vec3{ .x = 1.0f, .y = 0.0f, .z = 0.0f },
        0.5f,
        sample));
}

TEST(CollisionSurfaceSampling, HeightFieldRayHitsThroughWorldTranslation)
{
    const auto surface = flat_height_field_surface(10.0f);
    wz::math::Mat4 translated = wz::math::Mat4::identity();
    translated.m[12] = 100.0f;
    translated.m[13] = 200.0f;
    translated.m[14] = 300.0f;

    wz::engine::collision::CollisionSurfaceSample sample{};
    ASSERT_TRUE(wz::engine::collision::raycast_terrain_surface(
        surface_entry(surface, translated),
        wz::math::Vec3{ .x = 100.5f, .y = 220.0f, .z = 300.5f },
        wz::math::Vec3{ .x = 0.0f, .y = -1.0f, .z = 0.0f },
        100.0f,
        sample));
    EXPECT_TRUE(sample.hit);
    EXPECT_NEAR(sample.position.x, 100.5f, 1e-3f);
    EXPECT_NEAR(sample.position.y, 210.0f, 1e-3f);
    EXPECT_NEAR(sample.position.z, 300.5f, 1e-3f);
}

TEST(CollisionSurfaceSampling, HeightFieldRayHitsThroughLargeWorldScale)
{
    const auto surface = flat_height_field_surface(10.0f);  // footprint local [0,2]
    wz::math::Mat4 scaled = wz::math::Mat4::identity();
    scaled.m[0] = 1000.0f;
    scaled.m[5] = 1000.0f;
    scaled.m[10] = 1000.0f;

    wz::engine::collision::CollisionSurfaceSample sample{};
    ASSERT_TRUE(wz::engine::collision::raycast_terrain_surface(
        surface_entry(surface, scaled),
        wz::math::Vec3{ .x = 750.0f, .y = 20000.0f, .z = 750.0f },
        wz::math::Vec3{ .x = 0.0f, .y = -1.0f, .z = 0.0f },
        100000.0f,
        sample));
    EXPECT_TRUE(sample.hit);
    EXPECT_NEAR(sample.position.x, 750.0f, 1e-1f);
    EXPECT_NEAR(sample.position.y, 10000.0f, 1e-1f);
    EXPECT_NEAR(sample.position.z, 750.0f, 1e-1f);
}

TEST(CollisionSurfaceSampling, RaycastRejectsMeshSurfaceShape)
{
    // Mesh-surface ray-casting is serviced by the behavior adapter's triangle
    // path; raycast_terrain_surface fills only the heightfield gap and rejects
    // other shapes so the two paths never double-report.
    const auto surface = terrain_mesh_surface_with_triangle_in_cell(0u);
    wz::engine::collision::CollisionSurfaceSample sample{};
    EXPECT_FALSE(wz::engine::collision::raycast_terrain_surface(
        surface_entry(surface),
        wz::math::Vec3{ .x = 0.75f, .y = 10.0f, .z = 0.75f },
        wz::math::Vec3{ .x = 0.0f, .y = -1.0f, .z = 0.0f },
        100.0f,
        sample));
    EXPECT_FALSE(sample.hit);
}

TEST(CollisionSurfaceSampling, RaycastRejectsDegenerateDirection)
{
    const auto surface = flat_height_field_surface(10.0f);
    wz::engine::collision::CollisionSurfaceSample sample{};
    EXPECT_FALSE(wz::engine::collision::raycast_terrain_surface(
        surface_entry(surface),
        wz::math::Vec3{ .x = 0.5f, .y = 20.0f, .z = 0.5f },
        wz::math::Vec3{ .x = 0.0f, .y = 0.0f, .z = 0.0f },
        100.0f,
        sample));
}

namespace
{
    // A symmetric V-valley (flat in Z): sample column i has height |i-8| * 1.25,
    // so the floor is 0 at x=8 and the rims are 10 at x=0 and x=16. 17x17 samples
    // over a 16x16 footprint => sample step 1. The floor is a single breakpoint,
    // so a coarse render ring whose cell straddles it BRIDGES the floor with a
    // rim-to-rim chord, exactly like the drawn clipmap. render_lod_* mirror the
    // clipmap LOD schedule (0 = disabled).
    wz::engine::assets::CollisionAssetData valley_height_field_surface(
        uint32_t render_lod_base_resolution,
        uint32_t render_lod_level_count)
    {
        wz::engine::assets::CollisionAssetData surface{};
        surface.shape_kind =
            wz::engine::assets::CollisionShapeKind::TerrainHeightField;
        surface.occupancy.queryable = true;
        surface.origin[0] = 0.0f;
        surface.origin[1] = 0.0f;
        surface.size[0] = 16.0f;
        surface.size[1] = 16.0f;
        surface.resolution_x = 17u;
        surface.resolution_y = 17u;
        surface.vertical_scale = 1.0f;
        surface.base_height = 0.0f;
        surface.height_samples.resize(17u * 17u);
        for (uint32_t z = 0; z < 17u; ++z) {
            for (uint32_t x = 0; x < 17u; ++x) {
                int dx = static_cast<int>(x) - 8;
                if (dx < 0) {
                    dx = -dx;
                }
                surface.height_samples[z * 17u + x] =
                    static_cast<float>(dx) * 1.25f;
            }
        }
        surface.min_height = 0.0f;
        surface.max_height = 10.0f;
        surface.bounds_min[0] = 0.0f;
        surface.bounds_min[1] = 0.0f;
        surface.bounds_min[2] = 0.0f;
        surface.bounds_max[0] = 16.0f;
        surface.bounds_max[1] = 10.0f;
        surface.bounds_max[2] = 16.0f;
        surface.render_lod_base_resolution = render_lod_base_resolution;
        surface.render_lod_level_count = render_lod_level_count;
        return surface;
    }
}

TEST(CollisionSurfaceSampling, HeightFieldRayLodBridgesDistantValleyFloor)
{
    const auto lod = valley_height_field_surface(2u, 5u);
    const auto plain = valley_height_field_surface(0u, 0u);  // LOD disabled

    // A descending grazing shot from the near rim, out across the valley.
    const wz::math::Vec3 origin{ .x = 0.0f, .y = 14.0f, .z = 8.0f };
    const wz::math::Vec3 dir{ .x = 1.0f, .y = -1.0f, .z = 0.0f };

    wz::engine::collision::CollisionSurfaceSample plain_hit{};
    wz::engine::collision::CollisionSurfaceSample lod_hit{};
    ASSERT_TRUE(wz::engine::collision::raycast_terrain_surface(
        surface_entry(plain), origin, dir, 40.0f, plain_hit));
    ASSERT_TRUE(wz::engine::collision::raycast_terrain_surface(
        surface_entry(lod), origin, dir, 40.0f, lod_hit));

    // The true full-res ray skims over the valley floor and only strikes the far
    // rising rim (low + far). The drawn-surface ray strikes the coarse chord that
    // bridges the floor -- higher and nearer, where the hill is DRAWN -- instead
    // of passing through and reappearing on the far side.
    EXPECT_GT(lod_hit.position.y, plain_hit.position.y + 2.0f);
    EXPECT_LT(lod_hit.position.x, plain_hit.position.x - 1.0f);
}

TEST(CollisionSurfaceSampling, HeightFieldRayLodIsFullResUnderShooter)
{
    const auto lod = valley_height_field_surface(2u, 5u);
    const auto plain = valley_height_field_surface(0u, 0u);

    // Straight down onto the valley floor from directly above: the impact sits at
    // the ring center (the shooter), so it is in the level-0 full-res ring and
    // the drawn surface equals the true floor -- no bridging right under the shot.
    const wz::math::Vec3 origin{ .x = 8.0f, .y = 20.0f, .z = 8.0f };
    const wz::math::Vec3 dir{ .x = 0.0f, .y = -1.0f, .z = 0.0f };

    wz::engine::collision::CollisionSurfaceSample plain_hit{};
    wz::engine::collision::CollisionSurfaceSample lod_hit{};
    ASSERT_TRUE(wz::engine::collision::raycast_terrain_surface(
        surface_entry(plain), origin, dir, 30.0f, plain_hit));
    ASSERT_TRUE(wz::engine::collision::raycast_terrain_surface(
        surface_entry(lod), origin, dir, 30.0f, lod_hit));

    EXPECT_NEAR(plain_hit.position.y, 0.0f, 1e-2f);
    EXPECT_NEAR(lod_hit.position.y, plain_hit.position.y, 1e-2f);
}

// ── Constraining to the DRAWN surface (clipmap reconstruction) ───────────────

namespace
{
    // A 64x64 field over a 64 m footprint (so c0 = 1 m/texel) that is flat at
    // height 10 except for a narrow trench four texels wide running along Z.
    // Narrow is the point: a coarse clipmap ring samples every 2^L-th texel and
    // box-filters, so it BRIDGES a trench finer than its cell and draws ground
    // where the true field has a hole. An actor placed by the true field then
    // stands at the bottom of a trench the player cannot see.
    constexpr uint32_t kTrenchN = 64u;
    constexpr float kTrenchFloorX = 32.0f;
    constexpr float kRimHeight = 10.0f;

    wz::engine::assets::CollisionAssetData trench_surface(
        bool constrain_to_drawn_surface)
    {
        using namespace wz::engine::assets;

        std::vector<float> samples(
            static_cast<size_t>(kTrenchN) * kTrenchN, kRimHeight);
        for (uint32_t z = 0; z < kTrenchN; ++z) {
            for (uint32_t x = 0; x < kTrenchN; ++x) {
                const float d = std::abs(
                    static_cast<float>(x) - kTrenchFloorX);
                if (d < 2.0f) {
                    samples[static_cast<size_t>(z) * kTrenchN + x] =
                        kRimHeight * (d / 2.0f);
                }
            }
        }

        CollisionAssetData surface{};
        surface.shape_kind = CollisionShapeKind::TerrainHeightField;
        surface.occupancy.queryable = true;
        surface.supports_height_query = true;
        surface.resolution_x = kTrenchN;
        surface.resolution_y = kTrenchN;
        surface.origin[0] = 0.0f;
        surface.origin[1] = 0.0f;
        surface.size[0] = static_cast<float>(kTrenchN);
        surface.size[1] = static_cast<float>(kTrenchN);
        surface.min_height = 0.0f;
        surface.max_height = kRimHeight;
        surface.bounds_min[0] = 0.0f;
        surface.bounds_min[1] = 0.0f;
        surface.bounds_min[2] = 0.0f;
        surface.bounds_max[0] = static_cast<float>(kTrenchN);
        surface.bounds_max[1] = kRimHeight;
        surface.bounds_max[2] = static_cast<float>(kTrenchN);
        surface.height_samples = samples;

        // A clipmap schedule matching a lattice of 8 cells per ring over 4
        // rings: ring 0 reaches 4 m, ring 3 reaches 32 m.
        surface.render_lod_base_resolution = 8u;
        surface.render_lod_level_count = 4u;
        surface.render_lod_cell_size = 1.0f;
        surface.placement_driven = true;
        surface.constrain_to_drawn_surface = constrain_to_drawn_surface;

        // What the compiler builds; done by hand here through the same builder.
        const std::vector<internal::ScalarFieldMipLevel> pyramid =
            internal::build_scalar_field_mip_pyramid(
                surface.height_samples, kTrenchN, kTrenchN);
        for (size_t level = 1; level < pyramid.size(); ++level) {
            surface.height_mips.push_back(CollisionHeightMipLevel{
                pyramid[level].width,
                pyramid[level].height,
                pyramid[level].values });
        }
        return surface;
    }

    float sample_height_at(
        const wz::engine::assets::CollisionAssetData& surface,
        const wz::math::Vec3& observer)
    {
        wz::engine::collision::CollisionSurfaceSample sample{};
        EXPECT_TRUE(wz::engine::collision::sample_terrain_surface(
            surface_entry(surface),
            kTrenchFloorX,
            32.0f,
            sample,
            observer));
        return sample.position.y;
    }
}

// The behaviour this whole track exists for. Standing in the trench with the
// observer alongside, the finest ring covers the actor and the drawn ground IS
// the true ground. Move the observer away and the actor falls into a coarse
// ring whose cells bridge the trench -- so the drawn ground rises, and an actor
// constrained to it rises with it instead of sinking out of sight.
TEST(CollisionSurfaceSampling, DrawnSurfaceConstraintTracksTheObserver)
{
    const auto surface = trench_surface(/*constrain_to_drawn_surface*/ true);

    // Observer beside the actor: ring 0, cell 1 m, nothing decimated.
    const float near_height = sample_height_at(
        surface, wz::math::Vec3{ .x = kTrenchFloorX, .y = 0.0f, .z = 32.0f });
    EXPECT_NEAR(near_height, 0.0f, 0.5f)
        << "with the observer alongside, the drawn ground is the true ground";

    // Observer 20 m away: the actor is past rings 0-2, so it lands on ring 3,
    // whose 8 m cells cannot resolve a 4 m trench.
    const float far_height = sample_height_at(
        surface,
        wz::math::Vec3{ .x = kTrenchFloorX + 20.0f, .y = 0.0f, .z = 32.0f });
    EXPECT_GT(far_height, near_height + 2.0f)
        << "a coarse ring should bridge the trench and lift the drawn ground";
    EXPECT_LE(far_height, kRimHeight + 0.01f);
}

// Off by default, and off means genuinely view-independent: the same query
// answers identically however far the observer is, which is the property a
// physical surface has and the drawn one gives up.
TEST(CollisionSurfaceSampling, TrueSurfaceConstraintIgnoresTheObserver)
{
    const auto surface = trench_surface(/*constrain_to_drawn_surface*/ false);

    const float near_height = sample_height_at(
        surface, wz::math::Vec3{ .x = kTrenchFloorX, .y = 0.0f, .z = 32.0f });
    const float far_height = sample_height_at(
        surface,
        wz::math::Vec3{ .x = kTrenchFloorX + 20.0f, .y = 0.0f, .z = 32.0f });

    EXPECT_FLOAT_EQ(near_height, far_height);
}

// The drawn and true surfaces now share ONE sampling grid, which is the
// convention fix. They used to shear: the clipmap places texel i at
// origin + i*size/resolution, while the collision bicubic placed it at
// origin + i*size/(resolution-1), so the two grids drifted apart by up to half
// a texel across the footprint. That is 0.12 m on the live 4096-texel
// landscape and metres on a wall as steep as this trench -- an actor stood
// beside the ground it was drawn on rather than on it, whichever surface it was
// constrained to, and no amount of correct reconstruction could remove it.
//
// The convention is declared by the field's SOURCE rather than picked once
// globally: a placement-driven extent means N cells, which is what the
// Placement and the renderer both mean by it, while a standalone heightfield
// still spans first-sample-to-last.
TEST(CollisionSurfaceSampling, DrawnAndTrueSurfacesShareOneSamplingGrid)
{
    const auto drawn = trench_surface(/*constrain_to_drawn_surface*/ true);
    const auto truth = trench_surface(/*constrain_to_drawn_surface*/ false);

    // Observer alongside, so the actor sits in ring 0 and NOTHING is decimated:
    // any residue left here is pure grid disagreement.
    const wz::math::Vec3 observer{
        .x = kTrenchFloorX, .y = 0.0f, .z = 32.0f };

    // At exact sample positions the two must agree outright -- that is what
    // sharing a grid means. Walk across the trench so floor, wall and rim are
    // all covered.
    for (int i = -4; i <= 4; ++i) {
        const float x = kTrenchFloorX + static_cast<float>(i);
        wz::engine::collision::CollisionSurfaceSample drawn_sample{};
        wz::engine::collision::CollisionSurfaceSample truth_sample{};
        ASSERT_TRUE(wz::engine::collision::sample_terrain_surface(
            surface_entry(drawn), x, 32.0f, drawn_sample, observer));
        ASSERT_TRUE(wz::engine::collision::sample_terrain_surface(
            surface_entry(truth), x, 32.0f, truth_sample, observer));
        EXPECT_NEAR(drawn_sample.position.y, truth_sample.position.y, 1e-3f)
            << "grids disagree at x = " << x;
    }

    // And both find the trench floor where the data puts it.
    EXPECT_NEAR(sample_height_at(drawn, observer), 0.0f, 1e-3f);
    EXPECT_NEAR(sample_height_at(truth, observer), 0.0f, 1e-3f);
}

// Height moves to the drawn surface; ORIENTATION does not. The drawn surface is
// piecewise bilinear over cells as wide as a coarse ring, so its gradient is
// piecewise constant and an actor aligning to it would snap between facets. The
// normal stays on the smooth bicubic, so it is unchanged by the opt-in.
TEST(CollisionSurfaceSampling, DrawnSurfaceLeavesTheNormalOnTheSmoothField)
{
    const auto drawn = trench_surface(/*constrain_to_drawn_surface*/ true);
    const auto truth = trench_surface(/*constrain_to_drawn_surface*/ false);
    const wz::math::Vec3 observer{
        .x = kTrenchFloorX + 20.0f, .y = 0.0f, .z = 32.0f };

    // Off the trench floor, where the true surface has a real slope to report.
    const float probe_x = kTrenchFloorX + 1.0f;
    wz::engine::collision::CollisionSurfaceSample drawn_sample{};
    wz::engine::collision::CollisionSurfaceSample truth_sample{};
    ASSERT_TRUE(wz::engine::collision::sample_terrain_surface(
        surface_entry(drawn), probe_x, 32.0f, drawn_sample, observer));
    ASSERT_TRUE(wz::engine::collision::sample_terrain_surface(
        surface_entry(truth), probe_x, 32.0f, truth_sample, observer));

    EXPECT_NE(drawn_sample.position.y, truth_sample.position.y)
        << "the heights should differ -- otherwise this proves nothing";
    EXPECT_FLOAT_EQ(drawn_sample.normal.x, truth_sample.normal.x);
    EXPECT_FLOAT_EQ(drawn_sample.normal.y, truth_sample.normal.y);
    EXPECT_FLOAT_EQ(drawn_sample.normal.z, truth_sample.normal.z);
}

// ── Ray-march termination (issue #314, C1-C6) ────────────────────────────────
//
// The march advances by `t += step_t` and its ONLY exit is `t >= t1`. Float
// addition stops advancing once step_t falls below half an ULP of t, so a
// shooter far enough away in the collider's LOCAL frame spun forever. The
// stall is a BAND, not a threshold: it opens when ULP(t) > 2*step_t and closes
// again when ULP(t) > span (the span then collapses to a single float and the
// loop exits on its first iteration). Measured before the fix: with 1 m cells
// the hang starts at ~9e6, and with 10 cm cells at ~4e6 -- a FINER field hangs
// CLOSER IN, because step_t = 0.5 * cell.
//
// LOAD-BEARING SETUP, do not "simplify" it: the ray must travel HORIZONTALLY
// through the bounds box ABOVE the surface. A ray that hits exits the loop
// early and proves nothing, which is exactly how the first version of this
// probe passed against the unfixed code.
namespace
{
    wz::engine::assets::CollisionAssetData flat_field_for_march(float cell)
    {
        const uint32_t resolution = 5u;
        const float extent = 4.0f * cell;

        wz::engine::assets::CollisionAssetData surface{};
        surface.shape_kind =
            wz::engine::assets::CollisionShapeKind::TerrainHeightField;
        surface.occupancy.queryable = true;
        surface.bounds_min[0] = 0.0f;
        surface.bounds_min[1] = -1.0f;
        surface.bounds_min[2] = 0.0f;
        surface.bounds_max[0] = extent;
        surface.bounds_max[1] = 3.0f;
        surface.bounds_max[2] = extent;
        surface.origin[0] = 0.0f;
        surface.origin[1] = 0.0f;
        surface.size[0] = extent;
        surface.size[1] = extent;
        surface.resolution_x = resolution;
        surface.resolution_y = resolution;
        surface.vertical_scale = 1.0f;
        surface.height_samples.assign(
            static_cast<size_t>(resolution) * resolution, 0.0f);
        return surface;
    }

    bool march_across_field(float cell, float shooter_distance)
    {
        const auto surface = flat_field_for_march(cell);
        const float extent = 4.0f * cell;
        wz::engine::collision::CollisionSurfaceSample sample{};
        return wz::engine::collision::raycast_terrain_surface(
            surface_entry(surface),
            { extent * 0.5f + shooter_distance, 2.5f, extent * 0.5f },
            { -1.0f, 0.0f, 0.0f },
            shooter_distance * 2.0f + 100.0f,
            sample);
    }
}

// Without the iteration bound this test does not FAIL -- it HANGS, and ctest's
// per-test timeout is what turns it red. That is the regression it guards.
TEST(CollisionSurfaceSampling, RayMarchTerminatesFarFromTheColliderOrigin)
{
    // Control: near the field the step advances normally and the miss returns.
    EXPECT_FALSE(march_across_field(1.0f, 10.0f));

    // Inside the measured stall band for 1 m cells.
    EXPECT_FALSE(march_across_field(1.0f, 9.0e6f));
    EXPECT_FALSE(march_across_field(1.0f, 1.7e7f));

    // A finer field stalls closer in, so cover its band too.
    EXPECT_FALSE(march_across_field(0.1f, 4.0e6f));
    EXPECT_FALSE(march_across_field(0.1f, 8.0e6f));
}

// ── Hostile FIELD data, not hostile queries (issue #314, C1-C3) ─────────────
//
// The query side of every entry point was already finiteness-checked; the
// asset's own numbers were trusted. A NaN height sample or vertical_scale
// therefore produced `hit = true` with position = (nan, nan, nan) -- and the
// terrain-stick consumer (behavior_command_apply.cpp) writes that straight into
// a node's world translation, while its own `std::abs(nan - y) > 1e-6f` test
// reports "nothing changed".
//
// CollisionAssetData::valid() now refuses to build such an asset; this pins the
// runtime half, which is what protects a field that reached memory some other
// way. It hands the sampler a poisoned asset DIRECTLY for exactly that reason.
TEST(CollisionSurfaceSampling, NonFiniteFieldDataReportsNoHitRatherThanANaNHit)
{
    // This test feeds NaN field data on purpose, so the arithmetic that consumes
    // it raises FE_INVALID while doing exactly its job. Declared, so the FP
    // status listener does not report it as engine noise.
    wz::testing::ExpectFpException expected_fp{ FE_INVALID };

    // Control: the same field with finite data answers normally.
    {
        const auto healthy = flat_field_for_march(1.0f);
        wz::engine::collision::CollisionSurfaceSample sample{};
        ASSERT_TRUE(wz::engine::collision::sample_terrain_surface(
            surface_entry(healthy), 2.0f, 2.0f, sample));
        EXPECT_TRUE(std::isfinite(sample.position.y));
    }

    {
        auto poisoned = flat_field_for_march(1.0f);
        poisoned.vertical_scale = std::nanf("");
        wz::engine::collision::CollisionSurfaceSample sample{};
        EXPECT_FALSE(wz::engine::collision::sample_terrain_surface(
            surface_entry(poisoned), 2.0f, 2.0f, sample));
        EXPECT_FALSE(sample.hit);
        EXPECT_TRUE(std::isfinite(sample.position.y));
    }

    {
        auto poisoned = flat_field_for_march(1.0f);
        poisoned.height_samples[12] = std::nanf("");
        wz::engine::collision::CollisionSurfaceSample sample{};
        EXPECT_FALSE(wz::engine::collision::sample_terrain_surface(
            surface_entry(poisoned), 2.0f, 2.0f, sample));
        EXPECT_FALSE(sample.hit);
    }

    // The nearest-surface query clamps an off-field probe to the rim, so it
    // reaches the same reconstruction by a different path -- cover it too.
    {
        auto poisoned = flat_field_for_march(1.0f);
        poisoned.height_samples[12] = INFINITY;
        wz::engine::collision::CollisionSurfaceSample sample{};
        EXPECT_FALSE(wz::engine::collision::sample_nearest_terrain_surface(
            surface_entry(poisoned), 100.0f, 100.0f, sample));
    }
}
