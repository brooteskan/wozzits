#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/type_extensions.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

TEST(TerrainAssetModule, ResolvesHeightFieldTerrain)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_terrain_heightfield_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto field =
        assets.scalar_fields().create_procedural_scalar_field({
            .name = "terrain/height_gradient",
            .width = 8,
            .height = 4,
            .depth = 1,
            .generator = ScalarFieldGenerator::GradientX,
            .frequency = 1.0f,
            .amplitude = 2.0f,
            .format = ScalarFieldFormat::Float32,
            .domain_kind = ScalarFieldDomainKind::Spatial2D,
        });
    ASSERT_TRUE(field.valid());

    TerrainFromHeightFieldDesc desc{};
    desc.name = "terrain/heightfield";
    desc.height_field = field;
    desc.size[0] = 64.0f;
    desc.size[1] = 32.0f;
    desc.vertical_scale = 10.0f;
    desc.base_height = -5.0f;

    const auto terrain = assets.terrains().create_from_height_field(desc);
    ASSERT_TRUE(terrain.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto handle = assets.terrains().get_terrain(terrain);
    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(handle.handle.type, kAssetTypeTerrain);

    const auto* data = assets.terrains().get_terrain_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->representation, TerrainRepresentationKind::HeightField);
    EXPECT_EQ(data->height_field, field.output);
    EXPECT_EQ(data->resolution_x, 8u);
    EXPECT_EQ(data->resolution_y, 4u);
    EXPECT_FLOAT_EQ(data->size[0], 64.0f);
    EXPECT_FLOAT_EQ(data->size[1], 32.0f);
    EXPECT_FLOAT_EQ(data->min_height, -5.0f);
    EXPECT_FLOAT_EQ(data->max_height, 5.0f);
    EXPECT_TRUE(data->supports_height_query);
    EXPECT_FALSE(data->supports_ray_query);
}

TEST(TerrainAssetModule, ResolvesMeshTerrain)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_terrain_mesh_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto mesh =
        assets.meshes().create_procedural_mesh({
            .name = "terrain/mesh_quad",
            .kind = ProceduralMeshKind::Quad,
        });
    ASSERT_TRUE(mesh.valid());

    const auto terrain =
        assets.terrains().create_from_mesh({
            .name = "terrain/mesh_surface",
            .mesh = mesh,
        });
    ASSERT_TRUE(terrain.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto handle = assets.terrains().get_terrain(terrain);
    ASSERT_TRUE(handle.valid());

    const auto* data = assets.terrains().get_terrain_data(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->representation, TerrainRepresentationKind::MeshSurface);
    EXPECT_EQ(data->mesh, mesh.output);
    EXPECT_FLOAT_EQ(data->bounds_min[0], -1.0f);
    EXPECT_FLOAT_EQ(data->bounds_min[1], -1.0f);
    EXPECT_FLOAT_EQ(data->bounds_min[2], 0.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[0], 1.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[1], 1.0f);
    EXPECT_FLOAT_EQ(data->bounds_max[2], 0.0f);
    EXPECT_FALSE(data->supports_height_query);
    EXPECT_FALSE(data->supports_ray_query);
}
