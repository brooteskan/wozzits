#include <engine/collision/collision_surface_sampling.h>

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
