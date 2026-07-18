#include <engine/assets/engine_asset_library.h>
#include <engine/assets/collision/collision.h>
#include <engine/assets/key_factories/collision.h>
#include <engine/assets/placed_field_asset_module.h>
#include <engine/assets/schema_ids.h>

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

TEST(CollisionAssetModule, HeightFieldProducesConstraintCollisionAsset)
{
    const wz::fs::Path root =
        test_root("wozzits_collision_height_field_direct_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    // GradientX over a 4-wide field gives column values x/(width-1):
    // 0, 1/3, 2/3, 1 (constant across rows).
    const auto field = assets.scalar_fields().create_procedural_scalar_field({
        .name = "collision/direct_height_field",
        .width = 4,
        .height = 4,
        .depth = 1,
        .generator = ScalarFieldGenerator::GradientX,
    });
    ASSERT_TRUE(field.valid());

    constexpr float kVerticalScale = 4.0f;
    constexpr float kBaseHeight = -1.0f;

    const auto collision = assets.collisions().create_from_height_field({
        .name = "collision/direct_height_field_constraint",
        .height_field = field,
        .origin = { -2.0f, -3.0f },
        .size = { 10.0f, 12.0f },
        .vertical_scale = kVerticalScale,
        .base_height = kBaseHeight,
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
    EXPECT_EQ(data->shape_kind, CollisionShapeKind::TerrainHeightField);
    EXPECT_EQ(data->occupancy.kind, CollisionOccupancyKind::WalkableSurface);
    EXPECT_TRUE(data->supports_height_query);
    EXPECT_TRUE(data->supports_ray_query);
    EXPECT_EQ(data->height_field, field.output);
    EXPECT_EQ(data->source_asset, field.output);
    EXPECT_FLOAT_EQ(data->origin[0], -2.0f);
    EXPECT_FLOAT_EQ(data->origin[1], -3.0f);
    EXPECT_FLOAT_EQ(data->size[0], 10.0f);
    EXPECT_FLOAT_EQ(data->size[1], 12.0f);
    EXPECT_EQ(data->resolution_x, 4u);
    EXPECT_EQ(data->resolution_y, 4u);
    ASSERT_EQ(data->height_samples.size(), 16u);

    // World Y is baked: vertical_scale=1, base_height=0 on the data, with the
    // mapping folded into height_samples.
    EXPECT_FLOAT_EQ(data->vertical_scale, 1.0f);
    EXPECT_FLOAT_EQ(data->base_height, 0.0f);

    // Each row repeats the same column ramp baked through the mapping.
    for (uint32_t z = 0; z < 4u; ++z) {
        for (uint32_t x = 0; x < 4u; ++x) {
            const float field_value =
                static_cast<float>(x) / 3.0f;
            const float expected_world_y =
                kBaseHeight + field_value * kVerticalScale;
            EXPECT_FLOAT_EQ(
                data->height_samples[static_cast<size_t>(z) * 4u + x],
                expected_world_y)
                << "x=" << x << " z=" << z;
        }
    }

    EXPECT_FLOAT_EQ(data->min_height, kBaseHeight);
    EXPECT_FLOAT_EQ(data->max_height, kBaseHeight + kVerticalScale);
}

// A degenerate projection resolution (< 2, e.g. a stray 1 from the editor) must
// fall back to the field's native resolution rather than failing to compile a
// 1x1 heightfield (issue #216 — this was the "compiled height field collision
// asset is invalid" CompileFailed the user hit).
TEST(CollisionAssetModule, HeightFieldDegenerateProjectionResolutionFallsBackToNative)
{
    const wz::fs::Path root =
        test_root("wozzits_collision_height_field_degenerate_projection_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    using namespace wz::engine::assets;

    const auto field = assets.scalar_fields().create_procedural_scalar_field({
        .name = "collision/degenerate_projection_field",
        .width = 4,
        .height = 4,
        .depth = 1,
        .generator = ScalarFieldGenerator::GradientX,
    });
    ASSERT_TRUE(field.valid());

    const auto collision = assets.collisions().create_from_height_field({
        .name = "collision/degenerate_projection_constraint",
        .height_field = field,
        .projection_resolution_x = 1u,  // degenerate -> native (4)
        .projection_resolution_y = 1u,
    });
    ASSERT_TRUE(collision.valid());

    ASSERT_TRUE(assets.commit());
    EXPECT_TRUE(assets.resolve_all().ok());

    const CollisionAssetData* data =
        assets.collisions().get_collision_data(
            assets.collisions().get_collision(collision));
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->resolution_x, 4u);  // native, not 1
    EXPECT_EQ(data->resolution_y, 4u);
    EXPECT_EQ(data->height_samples.size(), 16u);
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

// ─── Placement asset (issue #218 Phase 1) ───────────────────────────────────

// A Placement frame compiles and round-trips its authored values. origin[1] is
// a mirror of base_height (single source of truth), extent.xz is the footprint
// and extent.y is the vertical scale.
TEST(PlacementAssetModule, PlacementCompilesAndRoundTrips)
{
    const wz::fs::Path root =
        test_root("wozzits_placement_round_trip_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto placement = assets.placements().create_placement({
        .name = "placement/frame",
        .origin = { 10.0f, 0.0f, 20.0f },
        .extent = { 500.0f, 30.0f, 600.0f },
        .base_height = 5.0f,
    });
    ASSERT_TRUE(placement.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const PlacementData* data =
        assets.placements().get_placement_data(
            assets.placements().get_placement(placement));
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_FLOAT_EQ(data->origin[0], 10.0f);
    EXPECT_FLOAT_EQ(data->origin[1], 5.0f);   // mirror of base_height
    EXPECT_FLOAT_EQ(data->origin[2], 20.0f);
    EXPECT_FLOAT_EQ(data->extent[0], 500.0f);
    EXPECT_FLOAT_EQ(data->extent[1], 30.0f);
    EXPECT_FLOAT_EQ(data->extent[2], 600.0f);
    EXPECT_FLOAT_EQ(data->base_height, 5.0f);
}

// When a Placement is connected, it is authoritative for the collision's
// origin / size / vertical_scale / base_height, OVERRIDING the recipe's own
// (deliberately different) params.
TEST(CollisionAssetModule, HeightFieldHonoursConnectedPlacement)
{
    const wz::fs::Path root =
        test_root("wozzits_collision_height_field_placement_tests");
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
        .name = "collision/placement_height_field",
        .width = 4,
        .height = 4,
        .depth = 1,
        .generator = ScalarFieldGenerator::GradientX,
    });
    ASSERT_TRUE(field.valid());

    // Placement: origin (10,_,20), footprint 500x600, vertical 30, base 5.
    const auto placement = assets.placements().create_placement({
        .name = "collision/placement_frame",
        .origin = { 10.0f, 0.0f, 20.0f },
        .extent = { 500.0f, 30.0f, 600.0f },
        .base_height = 5.0f,
    });
    ASSERT_TRUE(placement.valid());

    // The collision's OWN params are deliberately different from the placement.
    const auto collision = assets.collisions().create_from_height_field({
        .name = "collision/placement_constraint",
        .height_field = field,
        .placement = placement,
        .origin = { -2.0f, -3.0f },
        .size = { 10.0f, 12.0f },
        .vertical_scale = 1.0f,
        .base_height = -1.0f,
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
    EXPECT_EQ(data->shape_kind, CollisionShapeKind::TerrainHeightField);

    // A connected placement flags the data world-frame so the runtime skips
    // the carrying node transform (issue #224).
    EXPECT_TRUE(data->placement_driven);

    // The PLACEMENT's values win over the node's own params.
    EXPECT_FLOAT_EQ(data->origin[0], 10.0f);   // placement origin.x
    EXPECT_FLOAT_EQ(data->origin[1], 20.0f);   // placement origin.z (size mapping)
    EXPECT_FLOAT_EQ(data->size[0], 500.0f);    // placement extent.x
    EXPECT_FLOAT_EQ(data->size[1], 600.0f);    // placement extent.z

    // World footprint reflects the placement, not the params.
    EXPECT_FLOAT_EQ(data->bounds_min[0], 10.0f);
    EXPECT_FLOAT_EQ(data->bounds_min[2], 20.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[0], 10.0f + 500.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[2], 20.0f + 600.0f);

    // World Y baked from placement vertical_scale=30, base=5 over GradientX
    // (values 0..1): min == base, max == base + vertical_scale.
    EXPECT_FLOAT_EQ(data->min_height, 5.0f);
    EXPECT_FLOAT_EQ(data->max_height, 5.0f + 30.0f);
}

// #223 Seam 3: a height-field collision can take BOTH its scalar field and its
// world frame from a single PlacedField upstream — the same combiner the terrain
// clipmap reads. The PlacedField supersedes the separate field + placement ports
// so the collision and the visual terrain share one frame and cannot drift.
TEST(CollisionAssetModule, HeightFieldFromPlacedField)
{
    const wz::fs::Path root =
        test_root("wozzits_collision_placed_field_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    using namespace wz::engine::assets;

    const auto field = assets.scalar_fields().create_procedural_scalar_field({
        .name = "collision/placed_height_field",
        .width = 4,
        .height = 4,
        .depth = 1,
        .generator = ScalarFieldGenerator::GradientX,
    });
    ASSERT_TRUE(field.valid());

    // Same frame the clipmap would read: origin (10,_,20), footprint 500x600,
    // vertical 30, base 5.
    const auto placement = assets.placements().create_placement({
        .name = "collision/placed_frame",
        .origin = { 10.0f, 0.0f, 20.0f },
        .extent = { 500.0f, 30.0f, 600.0f },
        .base_height = 5.0f,
    });
    ASSERT_TRUE(placement.valid());

    // The single shared upstream binding the field to its frame.
    const auto placed = assets.placed_fields().create_placed_field({
        .field_key = field.output,
        .placement_key = placement.output,
        .field_type = kAssetTypeScalarField,
    });
    ASSERT_TRUE(placed.valid());

    // Author a collision node referencing ONLY the PlacedField (no direct field
    // or placement dep). The graph/editor path wires this through the new
    // placed_field port; here we register it directly. The key folds the
    // PlacedField so a change to it re-compiles the collision. The compile desc
    // carries no height field — the PlacedField supplies it.
    const float zero2[2] = { 0.0f, 0.0f };
    const CollisionOccupancyData occupancy{};
    const wz::asset::AssetKey collision_key =
        make_collision_from_height_field_key(
            "collision/from_placed_field",
            placed.output,
            zero2,
            zero2,
            1.0f,
            0.0f,
            occupancy);

    CollisionFromHeightFieldCompileDesc compile_desc{};
    wz::asset::AssetNode node{};
    node.key = collision_key;
    node.type = kAssetTypeCollisionAsset;
    node.schema = kCollisionFromHeightFieldSchema;
    node.stage = wz::asset::AssetStage::Source;
    node.payload = std::vector<uint8_t>{};
    node.meta = compile_desc;
    ASSERT_TRUE(assets.system().register_asset(
        std::move(node), { placed.output }));

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    EXPECT_TRUE(report.ok());

    const CollisionAssetData* data =
        assets.collisions().get_collision_data(
            assets.collisions().get_collision(
                CollisionAsset{ .output = collision_key }));
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->shape_kind, CollisionShapeKind::TerrainHeightField);

    // The height field the collision queries comes from the PlacedField's field.
    EXPECT_TRUE(data->height_field == field.output);

    // The PlacedField's placement is authoritative for the world frame — the
    // SAME values a clipmap on the same PlacedField carries, so they align.
    EXPECT_TRUE(data->placement_driven);
    EXPECT_FLOAT_EQ(data->origin[0], 10.0f);   // placement origin.x
    EXPECT_FLOAT_EQ(data->origin[1], 20.0f);   // placement origin.z
    EXPECT_FLOAT_EQ(data->size[0], 500.0f);    // placement extent.x
    EXPECT_FLOAT_EQ(data->size[1], 600.0f);    // placement extent.z
    EXPECT_FLOAT_EQ(data->min_height, 5.0f);            // base
    EXPECT_FLOAT_EQ(data->max_height, 5.0f + 30.0f);    // base + vertical
}

// Back-compat: with NO placement connected, the collision uses its own params
// exactly as before the Placement port existed.
TEST(CollisionAssetModule, HeightFieldWithoutPlacementUsesOwnParams)
{
    const wz::fs::Path root =
        test_root("wozzits_collision_height_field_no_placement_tests");
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
        .name = "collision/no_placement_height_field",
        .width = 4,
        .height = 4,
        .depth = 1,
        .generator = ScalarFieldGenerator::GradientX,
    });
    ASSERT_TRUE(field.valid());

    constexpr float kVerticalScale = 4.0f;
    constexpr float kBaseHeight = -1.0f;

    const auto collision = assets.collisions().create_from_height_field({
        .name = "collision/no_placement_constraint",
        .height_field = field,
        // no placement connected
        .origin = { -2.0f, -3.0f },
        .size = { 10.0f, 12.0f },
        .vertical_scale = kVerticalScale,
        .base_height = kBaseHeight,
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
    EXPECT_EQ(data->shape_kind, CollisionShapeKind::TerrainHeightField);

    // With no placement connected the data is NOT placement-driven, so the
    // runtime still composes the carrying node transform (#216 behaviour).
    EXPECT_FALSE(data->placement_driven);

    // The node's OWN params are used (unchanged from pre-#218 behaviour).
    EXPECT_FLOAT_EQ(data->origin[0], -2.0f);
    EXPECT_FLOAT_EQ(data->origin[1], -3.0f);
    EXPECT_FLOAT_EQ(data->size[0], 10.0f);
    EXPECT_FLOAT_EQ(data->size[1], 12.0f);
    EXPECT_FLOAT_EQ(data->min_height, kBaseHeight);
    EXPECT_FLOAT_EQ(data->max_height, kBaseHeight + kVerticalScale);
}

// The placement_driven flag (issue #224) survives the terrain-collision disk
// cache save/load round-trip.
TEST(CollisionAssetModule, PlacementDrivenFlagSurvivesDiskCache)
{
    const wz::fs::Path root =
        test_root("wozzits_collision_placement_disk_cache_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const wz::fs::Path cache_root =
        wz::fs::join(root, ".wozzits/cache");

    wz::Logger logger;
    wz::gpu::Device device{};

    using namespace wz::engine::assets;

    auto resolve_collision = [&]() -> CollisionAssetData {
        EngineAssetLibrary assets{
            device,
            logger,
            root,
            EngineAssetCacheSettings{
                .root = cache_root,
                .enabled = true,
            },
        };

        const auto field =
            assets.scalar_fields().create_procedural_scalar_field({
                .name = "collision/placement_cache_height_field",
                .width = 4,
                .height = 4,
                .depth = 1,
                .generator = ScalarFieldGenerator::GradientX,
            });
        EXPECT_TRUE(field.valid());

        const auto placement = assets.placements().create_placement({
            .name = "collision/placement_cache_frame",
            .origin = { 10.0f, 0.0f, 20.0f },
            .extent = { 500.0f, 30.0f, 600.0f },
            .base_height = 5.0f,
        });
        EXPECT_TRUE(placement.valid());

        const auto collision = assets.collisions().create_from_height_field({
            .name = "collision/placement_cache_constraint",
            .height_field = field,
            .placement = placement,
            .origin = { -2.0f, -3.0f },
            .size = { 10.0f, 12.0f },
            .vertical_scale = 1.0f,
            .base_height = -1.0f,
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

    const CollisionAssetData first = resolve_collision();
    ASSERT_TRUE(first.valid());
    EXPECT_TRUE(first.placement_driven);

    // Second resolve hits the disk cache written by the first; the flag must
    // survive the serialize / deserialize round-trip.
    const CollisionAssetData second = resolve_collision();
    ASSERT_TRUE(second.valid());
    EXPECT_TRUE(second.placement_driven);
    EXPECT_FLOAT_EQ(second.origin[0], first.origin[0]);
    EXPECT_FLOAT_EQ(second.origin[1], first.origin[1]);
    EXPECT_FLOAT_EQ(second.size[0], first.size[0]);
    EXPECT_FLOAT_EQ(second.size[1], first.size[1]);
    EXPECT_FLOAT_EQ(second.vertical_scale, first.vertical_scale);
    EXPECT_FLOAT_EQ(second.base_height, first.base_height);
}

// A connected ClipmapLatticeSchedule SUPERSEDES the authored render_lod_*
// params. Those describe how the clipmap DRAWS the field, so they belong to the
// lattice; hand-typing a copy on the collision is what let the reconstruction
// fall out of step with what is actually drawn. The authored values here (64/6)
// are the exact wrong pair test_mesh_001 carried, against the 128/7 the resolver
// really returns for a 4096 field at extent 1000 / horizon 1000 / budget 200k.
TEST(CollisionAssetModule, ConnectedScheduleSupersedesAuthoredRenderLod)
{
    const wz::fs::Path root =
        test_root("wozzits_collision_schedule_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    using namespace wz::engine::assets;

    // width is what the schedule reads as N; height stays at the collision's
    // minimum of 2 (it needs two samples per axis to bilinear-sample) so the
    // field costs 8K samples instead of 16M.
    const auto field = assets.scalar_fields().create_procedural_scalar_field({
        .name = "collision/schedule_height",
        .width = 4096,
        .height = 2,
        .depth = 1,
        .generator = ScalarFieldGenerator::GradientX,
    });
    ASSERT_TRUE(field.valid());

    const auto schedule =
        assets.clipmap_lattice_schedules().create_clipmap_lattice_schedule({
            .name = "collision/schedule",
            .field_key = field.output,
            .world_extent = 1000.0f,
            .horizon = 1000.0f,
            .triangle_budget = 200000u,
        });
    ASSERT_TRUE(schedule.valid());

    const float origin[2] = { 0.0f, 0.0f };
    const float size[2] = { 1000.0f, 1000.0f };
    const CollisionOccupancyData occupancy{};
    const wz::asset::AssetKey collision_key =
        make_collision_from_height_field_key(
            "collision/from_schedule",
            field.output,
            origin,
            size,
            1.0f,
            0.0f,
            occupancy,
            0,
            0,
            wz::asset::AssetKey{},
            64u,
            6u);

    CollisionFromHeightFieldCompileDesc compile_desc{};
    compile_desc.height_field = field.output;
    compile_desc.origin[0] = origin[0];
    compile_desc.origin[1] = origin[1];
    compile_desc.size[0] = size[0];
    compile_desc.size[1] = size[1];
    compile_desc.occupancy = occupancy;
    // The stale hand-typed pair the schedule must override.
    compile_desc.render_lod_base_resolution = 64u;
    compile_desc.render_lod_level_count = 6u;

    wz::asset::AssetNode node{};
    node.key = collision_key;
    node.type = kAssetTypeCollisionAsset;
    node.schema = kCollisionFromHeightFieldSchema;
    node.stage = wz::asset::AssetStage::Source;
    node.payload = std::vector<uint8_t>{};
    node.meta = compile_desc;
    ASSERT_TRUE(assets.system().register_asset(
        std::move(node), { field.output, schedule.output }));

    ASSERT_TRUE(assets.commit());
    EXPECT_TRUE(assets.resolve_all().ok());

    const CollisionAssetData* data =
        assets.collisions().get_collision_data(
            assets.collisions().get_collision(
                CollisionAsset{ .output = collision_key }));
    ASSERT_NE(data, nullptr);
    ASSERT_TRUE(data->valid());

    // The schedule's resolved lattice, not the authored 64/6.
    EXPECT_EQ(data->render_lod_base_resolution, 128u);
    EXPECT_EQ(data->render_lod_level_count, 7u);
}

// The render-LOD reconstruction schedule (base_resolution m + level_count)
// lands in the compiled CollisionAssetData, so it has to be part of the key:
// while it was omitted, two collisions differing only in their schedule shared
// a key and the second silently served the first's cached artifact.
TEST(CollisionAssetModule, RenderLodScheduleIsPartOfTheHeightFieldKey)
{
    using namespace wz::engine::assets;

    const float origin[2]{ 0.0f, 0.0f };
    const float size[2]{ 1.0f, 1.0f };
    const CollisionOccupancyData occupancy{};
    const wz::asset::AssetKey field{};

    const auto key_for = [&](uint32_t m, uint32_t levels) {
        return make_collision_from_height_field_key(
            "collision/schedule",
            field,
            origin,
            size,
            1.0f,
            0.0f,
            occupancy,
            0,
            0,
            wz::asset::AssetKey{},
            m,
            levels);
    };

    // Disabled is the default and describes every asset authored before the
    // schedule existed, so it must reproduce the original key byte for byte.
    EXPECT_TRUE(
        key_for(0u, 0u)
        == make_collision_from_height_field_key(
            "collision/schedule",
            field,
            origin,
            size,
            1.0f,
            0.0f,
            occupancy));

    // The live mismatch this guards: resolve_clipmap_lattice returns (m=128,
    // L=7) for test_mesh_001 while the collision node is hand-authored (64, 6).
    EXPECT_FALSE(key_for(128u, 7u) == key_for(64u, 6u));

    // Either component alone must move the key.
    EXPECT_FALSE(key_for(128u, 7u) == key_for(64u, 7u));
    EXPECT_FALSE(key_for(128u, 7u) == key_for(128u, 6u));
    EXPECT_FALSE(key_for(128u, 7u) == key_for(0u, 0u));
}
