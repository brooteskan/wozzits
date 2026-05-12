// tests/asset_mesh/mesh_asset_module.cpp

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

namespace
{
    wz::engine::assets::EngineAssetLibrary make_assets(
        wz::gpu::Device& device,
        wz::Logger& logger)
    {
        const wz::fs::Path root =
            wz::fs::join(
                wz::fs::temp_directory_path(),
                "wozzits_mesh_asset_tests");

        EXPECT_EQ(
            wz::fs::create_directories(root),
            wz::fs::FileError::None);

        return wz::engine::assets::EngineAssetLibrary(
            device,
            logger,
            root);
    }
}

TEST(MeshAssetModule, ResolvesProceduralTriangleMesh)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    auto assets = make_assets(device, logger);

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "triangle",
        .kind = wz::engine::assets::ProceduralMeshKind::Triangle,
        });

    ASSERT_TRUE(mesh.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 1u);

    const auto handle = assets.meshes().get_mesh(mesh);

    ASSERT_TRUE(handle.valid());

    const auto* data = assets.meshes().get_mesh_data(handle);

    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->vertex_count(), 3u);
    EXPECT_EQ(data->index_count(), 3u);
}

TEST(MeshAssetModule, ResolvesProceduralQuadMesh)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    auto assets = make_assets(device, logger);

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "quad",
        .kind = wz::engine::assets::ProceduralMeshKind::Quad,
        });

    ASSERT_TRUE(mesh.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 1u);

    const auto handle = assets.meshes().get_mesh(mesh);

    ASSERT_TRUE(handle.valid());

    const auto* data = assets.meshes().get_mesh_data(handle);

    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->vertex_count(), 4u);
    EXPECT_EQ(data->index_count(), 6u);
}

TEST(MeshAssetModule, ResolvesProceduralCubeMesh)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    auto assets = make_assets(device, logger);

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "cube",
        .kind = wz::engine::assets::ProceduralMeshKind::Cube,
        });

    ASSERT_TRUE(mesh.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 1u);

    const auto handle = assets.meshes().get_mesh(mesh);

    ASSERT_TRUE(handle.valid());

    const auto* data = assets.meshes().get_mesh_data(handle);

    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->vertex_count(), 8u);
    EXPECT_EQ(data->index_count(), 36u);
}

TEST(MeshAssetModule, ResolvesAllProceduralMeshKinds)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    auto assets = make_assets(device, logger);

    const auto triangle = assets.meshes().create_procedural_mesh({
        .name = "triangle",
        .kind = wz::engine::assets::ProceduralMeshKind::Triangle,
        });

    const auto quad = assets.meshes().create_procedural_mesh({
        .name = "quad",
        .kind = wz::engine::assets::ProceduralMeshKind::Quad,
        });

    const auto cube = assets.meshes().create_procedural_mesh({
        .name = "cube",
        .kind = wz::engine::assets::ProceduralMeshKind::Cube,
        });

    ASSERT_TRUE(triangle.valid());
    ASSERT_TRUE(quad.valid());
    ASSERT_TRUE(cube.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 3u);

    const auto triangle_handle = assets.meshes().get_mesh(triangle);
    const auto quad_handle = assets.meshes().get_mesh(quad);
    const auto cube_handle = assets.meshes().get_mesh(cube);

    ASSERT_TRUE(triangle_handle.valid());
    ASSERT_TRUE(quad_handle.valid());
    ASSERT_TRUE(cube_handle.valid());

    const auto* triangle_data =
        assets.meshes().get_mesh_data(triangle_handle);
    const auto* quad_data =
        assets.meshes().get_mesh_data(quad_handle);
    const auto* cube_data =
        assets.meshes().get_mesh_data(cube_handle);

    ASSERT_NE(triangle_data, nullptr);
    ASSERT_NE(quad_data, nullptr);
    ASSERT_NE(cube_data, nullptr);

    EXPECT_EQ(triangle_data->vertex_count(), 3u);
    EXPECT_EQ(triangle_data->index_count(), 3u);

    EXPECT_EQ(quad_data->vertex_count(), 4u);
    EXPECT_EQ(quad_data->index_count(), 6u);

    EXPECT_EQ(cube_data->vertex_count(), 8u);
    EXPECT_EQ(cube_data->index_count(), 36u);
}