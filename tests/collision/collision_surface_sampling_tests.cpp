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
