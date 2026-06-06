#include <engine/assets/engine_asset_library.h>

#include <gtest/gtest.h>

namespace
{
    wz::fs::Path test_root(const char* name)
    {
        return wz::fs::join(wz::fs::temp_directory_path(), name);
    }
}

TEST(CollisionAssetModule, MeshCanProduceTriangleCollisionAsset)
{
    const wz::fs::Path root =
        test_root("wozzits_collision_mesh_triangle_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "collision/source_cube",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    CollisionOccupancyData occupancy{};
    occupancy.kind = CollisionOccupancyKind::Solid;
    occupancy.blocks_movement = true;
    occupancy.queryable = true;

    const auto collision = assets.collisions().create_from_mesh({
        .name = "collision/cube_solid",
        .mesh = mesh,
        .build_method = CollisionBuildMethod::TriangleMesh,
        .occupancy = occupancy,
    });
    ASSERT_TRUE(collision.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const CollisionHandle handle =
        assets.collisions().get_collision(collision);
    ASSERT_TRUE(handle.valid());

    const CollisionAssetData* data =
        assets.collisions().get_collision_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->source_kind, CollisionSourceKind::Mesh);
    EXPECT_EQ(data->shape_kind, CollisionShapeKind::TriangleMesh);
    EXPECT_EQ(data->occupancy.kind, CollisionOccupancyKind::Solid);
    EXPECT_TRUE(data->occupancy.blocks_movement);
    EXPECT_TRUE(data->occupancy.queryable);
    EXPECT_TRUE(data->supports_bounds_query);
    EXPECT_TRUE(data->supports_ray_query);
    EXPECT_TRUE(data->supports_overlap_query);
    EXPECT_GT(data->points.size(), 0u);
    EXPECT_GT(data->indices.size(), 0u);
    EXPECT_EQ(data->source_asset, mesh.output);
    EXPECT_EQ(data->geometry_asset, mesh.output);
}

TEST(CollisionAssetModule, MeshCanProduceBoundsSensorAsset)
{
    const wz::fs::Path root =
        test_root("wozzits_collision_mesh_bounds_sensor_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "collision/source_bounds_cube",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    CollisionOccupancyData occupancy{};
    occupancy.kind = CollisionOccupancyKind::Sensor;
    occupancy.blocks_movement = false;
    occupancy.queryable = true;

    const auto collision = assets.collisions().create_from_mesh({
        .name = "collision/cube_sensor_bounds",
        .mesh = mesh,
        .build_method = CollisionBuildMethod::Bounds,
        .occupancy = occupancy,
    });
    ASSERT_TRUE(collision.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const CollisionAssetData* data =
        assets.collisions().get_collision_data(
            assets.collisions().get_collision(collision));
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->shape_kind, CollisionShapeKind::Bounds);
    EXPECT_EQ(data->occupancy.kind, CollisionOccupancyKind::Sensor);
    EXPECT_FALSE(data->occupancy.blocks_movement);
    EXPECT_TRUE(data->supports_overlap_query);
    EXPECT_FALSE(data->supports_ray_query);
    EXPECT_TRUE(data->points.empty());
    EXPECT_TRUE(data->indices.empty());
}

TEST(CollisionAssetModule, HeightfieldTerrainProducesHeightCollisionAsset)
{
    const wz::fs::Path root =
        test_root("wozzits_collision_heightfield_terrain_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto field = assets.scalar_fields().create_procedural_scalar_field({
        .name = "collision/height_field",
        .width = 4,
        .height = 4,
        .depth = 1,
        .generator = ScalarFieldGenerator::GradientX,
    });
    ASSERT_TRUE(field.valid());

    const auto terrain = assets.terrains().create_from_height_field({
        .name = "collision/height_terrain",
        .height_field = field,
        .origin = { -2.0f, -3.0f },
        .size = { 10.0f, 12.0f },
        .vertical_scale = 4.0f,
        .base_height = -1.0f,
    });
    ASSERT_TRUE(terrain.valid());

    const auto collision = assets.collisions().create_from_terrain({
        .name = "collision/height_terrain_walkable",
        .terrain = terrain,
    });
    ASSERT_TRUE(collision.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const CollisionAssetData* data =
        assets.collisions().get_collision_data(
            assets.collisions().get_collision(collision));
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->source_kind, CollisionSourceKind::Terrain);
    EXPECT_EQ(data->shape_kind, CollisionShapeKind::TerrainHeightField);
    EXPECT_EQ(data->occupancy.kind, CollisionOccupancyKind::WalkableSurface);
    EXPECT_TRUE(data->supports_height_query);
    EXPECT_TRUE(data->supports_ray_query);
    EXPECT_EQ(data->source_asset, terrain.output);
    EXPECT_EQ(data->height_field, field.output);
    EXPECT_FLOAT_EQ(data->origin[0], -2.0f);
    EXPECT_FLOAT_EQ(data->origin[1], -3.0f);
    EXPECT_FLOAT_EQ(data->size[0], 10.0f);
    EXPECT_FLOAT_EQ(data->size[1], 12.0f);
    EXPECT_EQ(data->resolution_x, 4u);
    EXPECT_EQ(data->resolution_y, 4u);
    EXPECT_EQ(data->height_samples.size(), 16u);
}

TEST(CollisionAssetModule, MeshTerrainProducesTerrainMeshSurfaceAsset)
{
    const wz::fs::Path root =
        test_root("wozzits_collision_mesh_terrain_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "collision/source_terrain_quad",
        .kind = ProceduralMeshKind::Quad,
    });
    ASSERT_TRUE(mesh.valid());

    const auto terrain = assets.terrains().create_from_mesh({
        .name = "collision/mesh_terrain",
        .mesh = mesh,
        .min_surface_normal_y = 0.0f,
        .include_backfaces = true,
    });
    ASSERT_TRUE(terrain.valid());

    const auto collision = assets.collisions().create_from_terrain({
        .name = "collision/mesh_terrain_surface",
        .terrain = terrain,
    });
    ASSERT_TRUE(collision.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const CollisionAssetData* data =
        assets.collisions().get_collision_data(
            assets.collisions().get_collision(collision));
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->source_kind, CollisionSourceKind::Terrain);
    EXPECT_EQ(data->shape_kind, CollisionShapeKind::TerrainMeshSurface);
    EXPECT_EQ(data->source_asset, terrain.output);
    EXPECT_EQ(data->geometry_asset, mesh.output);
    EXPECT_EQ(data->mesh, mesh.output);
    EXPECT_GT(data->source_triangle_count, 0u);
    EXPECT_GT(data->accepted_triangle_count, 0u);
}

TEST(CollisionAssetModule, TerrainCollisionUsesProjectDiskCache)
{
    const wz::fs::Path root =
        test_root("wozzits_collision_terrain_disk_cache_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const wz::fs::Path cache_root =
        wz::fs::join(root, ".wozzits/cache");

    wz::Logger logger;
    wz::gpu::Device device{};

    using namespace wz::engine::assets;

    auto resolve_collision =
        [&](const char* mesh_name,
            const char* terrain_name,
            const char* collision_name) -> CollisionAssetData
    {
        EngineAssetLibrary assets{
            device,
            logger,
            root,
            EngineAssetCacheSettings{
                .root = cache_root,
                .enabled = true,
            },
        };

        const auto mesh = assets.meshes().create_procedural_mesh({
            .name = mesh_name,
            .kind = ProceduralMeshKind::Cube,
        });
        EXPECT_TRUE(mesh.valid());

        const auto terrain = assets.terrains().create_from_mesh({
            .name = terrain_name,
            .mesh = mesh,
            .min_surface_normal_y = 0.0f,
            .include_backfaces = true,
        });
        EXPECT_TRUE(terrain.valid());

        const auto collision = assets.collisions().create_from_terrain({
            .name = collision_name,
            .terrain = terrain,
        });
        EXPECT_TRUE(collision.valid());

        EXPECT_TRUE(assets.commit());
        const auto report = assets.resolve_all();
        EXPECT_TRUE(report.ok());

        const CollisionAssetData* data =
            assets.collisions().get_collision_data(
                assets.collisions().get_collision(collision));
        EXPECT_NE(data, nullptr);
        return data ? *data : CollisionAssetData{};
    };

    const CollisionAssetData first = resolve_collision(
        "collision/cache_source_cube",
        "collision/cache_mesh_terrain",
        "collision/cache_terrain_surface");
    ASSERT_TRUE(first.valid());

    const wz::fs::Path cache_directory =
        wz::fs::join(
            wz::fs::join(cache_root, "assets"),
            "collision_terrain");
    const auto entries = wz::fs::list_directory(cache_directory);
    ASSERT_EQ(entries.error, wz::fs::FileError::None);
    EXPECT_FALSE(entries.value.empty());

    const CollisionAssetData second = resolve_collision(
        "collision/cache_source_cube",
        "collision/cache_mesh_terrain",
        "collision/cache_terrain_surface");
    ASSERT_TRUE(second.valid());

    EXPECT_EQ(second.shape_kind, first.shape_kind);
    EXPECT_EQ(second.points.size(), first.points.size());
    EXPECT_EQ(second.indices.size(), first.indices.size());
    EXPECT_EQ(second.triangle_bounds.size(), first.triangle_bounds.size());
    EXPECT_EQ(
        second.surface_grid.cell_triangle_indices.size(),
        first.surface_grid.cell_triangle_indices.size());
    EXPECT_EQ(second.source_triangle_count, first.source_triangle_count);
    EXPECT_EQ(second.accepted_triangle_count, first.accepted_triangle_count);
}

TEST(CollisionAssetModule, MeshTerrainProducesRegularProjectionHeightField)
{
    const wz::fs::Path root =
        test_root("wozzits_collision_mesh_terrain_projection_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "collision/source_projection_cube",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const auto terrain = assets.terrains().create_from_mesh({
        .name = "collision/projection_mesh_terrain",
        .mesh = mesh,
        .min_surface_normal_y = 0.0f,
        .include_backfaces = true,
    });
    ASSERT_TRUE(terrain.valid());

    const auto collision = assets.collisions().create_from_terrain({
        .name = "collision/mesh_terrain_projection",
        .terrain = terrain,
        .build_method = CollisionBuildMethod::TerrainProjectionHeightField,
        .projection_resolution_x = 17u,
        .projection_resolution_y = 9u,
    });
    ASSERT_TRUE(collision.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    if (!report.ok()) {
        ADD_FAILURE() << "resolve_all failed with "
                      << report.failures.size() << " failure(s)";
        for (const auto& failure : report.failures) {
            ADD_FAILURE() << "  error=" << static_cast<int>(failure.error);
        }
    }
    ASSERT_TRUE(report.ok());

    const CollisionAssetData* data =
        assets.collisions().get_collision_data(
            assets.collisions().get_collision(collision));
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->source_kind, CollisionSourceKind::Terrain);
    EXPECT_EQ(data->shape_kind, CollisionShapeKind::TerrainHeightField);
    EXPECT_EQ(data->source_asset, terrain.output);
    EXPECT_EQ(data->geometry_asset, mesh.output);
    EXPECT_EQ(data->mesh, mesh.output);
    EXPECT_EQ(data->resolution_x, 17u);
    EXPECT_EQ(data->resolution_y, 9u);
    EXPECT_EQ(data->height_samples.size(), 17u * 9u);
    EXPECT_TRUE(data->supports_height_query);
    EXPECT_TRUE(data->supports_ray_query);
    EXPECT_GT(data->source_triangle_count, 0u);
    EXPECT_GT(data->accepted_triangle_count, 0u);
}
