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
    EXPECT_TRUE(data->has_normals);
    EXPECT_TRUE(data->has_uv0);
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
    EXPECT_TRUE(data->has_normals);
    EXPECT_TRUE(data->has_uv0);
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

TEST(MeshAssetModule, ResolvesDecimatedMeshAsset)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    auto assets = make_assets(device, logger);

    const auto source = assets.meshes().create_procedural_mesh({
        .name = "cube_source",
        .kind = wz::engine::assets::ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(source.valid());

    const auto decimated = assets.meshes().create_decimated_mesh({
        .name = "cube_decimated",
        .source_mesh = source,
        .target_vertex_count = 6u,
        .preserve_boundary = true,
        });
    ASSERT_TRUE(decimated.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto source_handle = assets.meshes().get_mesh(source);
    const auto decimated_handle = assets.meshes().get_mesh(decimated);
    ASSERT_TRUE(source_handle.valid());
    ASSERT_TRUE(decimated_handle.valid());

    const auto* source_data = assets.meshes().get_mesh_data(source_handle);
    const auto* decimated_data =
        assets.meshes().get_mesh_data(decimated_handle);

    ASSERT_NE(source_data, nullptr);
    ASSERT_NE(decimated_data, nullptr);
    EXPECT_TRUE(decimated_data->valid());
    EXPECT_LE(decimated_data->vertex_count(), source_data->vertex_count());
    EXPECT_LE(decimated_data->index_count(), source_data->index_count());
}

TEST(MeshAssetModule, ResolvesIdentityMeshClusterHierarchyAsset)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    auto assets = make_assets(device, logger);

    const auto source = assets.meshes().create_procedural_mesh({
        .name = "cube_source",
        .kind = wz::engine::assets::ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(source.valid());

    const auto hierarchy =
        assets.mesh_cluster_hierarchies().create_mesh_cluster_hierarchy({
            .name = "cube_identity_hierarchy",
            .source_mesh = source,
            .method =
                wz::engine::assets::MeshClusterHierarchyBuildMethod::Identity,
        });
    ASSERT_TRUE(hierarchy.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto source_handle = assets.meshes().get_mesh(source);
    const auto hierarchy_handle =
        assets.mesh_cluster_hierarchies().get_mesh_cluster_hierarchy(
            hierarchy);
    ASSERT_TRUE(source_handle.valid());
    ASSERT_TRUE(hierarchy_handle.valid());

    const auto* source_data = assets.meshes().get_mesh_data(source_handle);
    const auto* hierarchy_data =
        assets.mesh_cluster_hierarchies()
            .get_mesh_cluster_hierarchy_data(hierarchy_handle);

    ASSERT_NE(source_data, nullptr);
    ASSERT_NE(hierarchy_data, nullptr);
    ASSERT_TRUE(hierarchy_data->valid());
    EXPECT_EQ(hierarchy_data->source_mesh_key, source.output);
    EXPECT_EQ(
        hierarchy_data->method,
        wz::engine::assets::MeshClusterHierarchyBuildMethod::Identity);
    ASSERT_EQ(hierarchy_data->level_count(), 1u);

    const auto& level = hierarchy_data->levels[0];
    EXPECT_EQ(level.level_index, 0u);
    EXPECT_EQ(level.cluster_count, 1u);
    EXPECT_EQ(level.vertex_count, source_data->vertex_count());
    EXPECT_EQ(level.triangle_count, source_data->index_count() / 3u);
    EXPECT_FLOAT_EQ(level.conservative_error, 0.0f);
    EXPECT_EQ(level.preview_mesh.vertex_count(), source_data->vertex_count());
    EXPECT_EQ(level.preview_mesh.index_count(), source_data->index_count());
}

TEST(MeshAssetModule, RejectsUnsupportedMeshClusterHierarchyMethod)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    auto assets = make_assets(device, logger);

    const auto source = assets.meshes().create_procedural_mesh({
        .name = "cube_source",
        .kind = wz::engine::assets::ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(source.valid());

    const auto hierarchy =
        assets.mesh_cluster_hierarchies().create_mesh_cluster_hierarchy({
            .name = "cube_graph_hierarchy",
            .source_mesh = source,
            .method =
                wz::engine::assets::MeshClusterHierarchyBuildMethod::
                    GraphCoarsen,
        });

    EXPECT_FALSE(hierarchy.valid());
}

TEST(MeshAssetModule, ResolvesMeshClusterHierarchyPreviewMesh)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    auto assets = make_assets(device, logger);

    const auto source = assets.meshes().create_procedural_mesh({
        .name = "cube_source",
        .kind = wz::engine::assets::ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(source.valid());

    const auto hierarchy =
        assets.mesh_cluster_hierarchies().create_mesh_cluster_hierarchy({
            .name = "cube_identity_hierarchy",
            .source_mesh = source,
            .method =
                wz::engine::assets::MeshClusterHierarchyBuildMethod::Identity,
        });
    ASSERT_TRUE(hierarchy.valid());

    const auto preview =
        assets.mesh_cluster_hierarchies()
            .create_mesh_cluster_hierarchy_preview_mesh({
                .name = "cube_identity_preview_level_0",
                .hierarchy = hierarchy,
                .level_index = 0u,
            });
    ASSERT_TRUE(preview.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 3u);

    const auto source_handle = assets.meshes().get_mesh(source);
    const auto preview_handle = assets.meshes().get_mesh(preview);
    ASSERT_TRUE(source_handle.valid());
    ASSERT_TRUE(preview_handle.valid());

    const auto* source_data = assets.meshes().get_mesh_data(source_handle);
    const auto* preview_data = assets.meshes().get_mesh_data(preview_handle);

    ASSERT_NE(source_data, nullptr);
    ASSERT_NE(preview_data, nullptr);
    EXPECT_TRUE(preview_data->valid());
    EXPECT_EQ(preview_data->vertex_count(), source_data->vertex_count());
    EXPECT_EQ(preview_data->index_count(), source_data->index_count());
}

TEST(MeshAssetModule, ResolvesDefaultPlaceholderMesh)
{
    wz::Logger logger;
    wz::gpu::Device device{};

    auto assets = make_assets(device, logger);

    const auto mesh = assets.meshes().create_placeholder_mesh();

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
    EXPECT_TRUE(data->has_normals);
    EXPECT_FALSE(data->has_uv0);
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
