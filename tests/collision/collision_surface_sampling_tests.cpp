#include <engine/collision/collision_surface_sampling.h>

#include <engine/assets/scalar_field/scalar_field_compilers.h>

#include <cmath>
#include <vector>

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

// The two surfaces do NOT coincide even under the observer, and it is worth
// knowing why before reading a height difference as a reconstruction error.
//
// They disagree about where a sample SITS. The drawn surface follows the
// clipmap, which spreads `resolution` texels across the footprint (texel i at
// origin + i*size/resolution). The bicubic spreads `resolution - 1` intervals
// across it (texel i at origin + i*size/(resolution-1)). The grids therefore
// shear apart by up to half a texel across the footprint, and on a slope that
// is a height difference no amount of correct reconstruction removes.
//
// Half a texel of a 64 m / 64 texel field is 0.5 m, which on this deliberately
// steep trench wall is metres of height; on the live 4096-texel landscape it is
// 0.12 m. Unifying the two conventions is a separate change -- this test exists
// so the offset is a recorded fact rather than a surprise in a bug report.
TEST(CollisionSurfaceSampling, DrawnAndTrueSurfacesShearByTheSamplingConvention)
{
    const auto drawn = trench_surface(/*constrain_to_drawn_surface*/ true);
    const auto truth = trench_surface(/*constrain_to_drawn_surface*/ false);

    // Observer alongside, so the actor is in ring 0 and NO decimation applies:
    // whatever is left is the convention shear alone.
    const wz::math::Vec3 observer{
        .x = kTrenchFloorX, .y = 0.0f, .z = 32.0f };
    const float drawn_height = sample_height_at(drawn, observer);
    const float true_height = sample_height_at(truth, observer);

    // The drawn side reads the trench floor; the bicubic reads half a texel up
    // the wall. Both are "correct" for their own grid.
    EXPECT_NEAR(drawn_height, 0.0f, 0.5f);
    EXPECT_GT(true_height, drawn_height);

    // Bounded by the shear, not unbounded: half a texel up a wall that climbs
    // kRimHeight over two texels.
    EXPECT_LT(true_height - drawn_height, 0.5f * kRimHeight);
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
