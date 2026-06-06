// file: tests/asset/gltf_importer.cpp

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

#include <engine/assets/gltf/gltf_importer.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>


#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <asset/types.h>


namespace
{
    std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return {};

        file.seekg(0, std::ios::end);
        const auto size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        file.read(reinterpret_cast<char*>(bytes.data()), size);

        return bytes;
    }

#ifndef WZ_TEST_FIXTURE_DIR
#define WZ_TEST_FIXTURE_DIR "tests/fixtures"
#endif

    std::filesystem::path fixture_path(const char* relative)
    {
        return std::filesystem::path(WZ_TEST_FIXTURE_DIR) / relative;
    }
}

TEST(GLTFImporter, RejectsEmptyInput)
{
    wz::engine::assets::ImportedGLTFMeshSet out;
    wz::engine::assets::GLTFImportOptions options;

    EXPECT_FALSE(wz::engine::assets::import_glb_meshes(nullptr, 0, options, out));
    EXPECT_TRUE(out.meshes.empty());
}

TEST(GLTFImporter, RejectsInvalidGLBBytes)
{
    const std::uint8_t bad_bytes[] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    wz::engine::assets::ImportedGLTFMeshSet out;
    wz::engine::assets::GLTFImportOptions options;

    EXPECT_FALSE(wz::engine::assets::import_glb_meshes(
        bad_bytes,
        sizeof(bad_bytes),
        options,
        out));

    EXPECT_TRUE(out.meshes.empty());
}

TEST(GLTFImporter, ImportsCubeGLBAsMeshData)
{
    const auto path = fixture_path("gltf/low_poly_rock.glb");
    SCOPED_TRACE(path.string());

    const auto bytes = read_binary_file(path);
    ASSERT_FALSE(bytes.empty());

    wz::engine::assets::ImportedGLTFMeshSet out;
    wz::engine::assets::GLTFImportOptions options;

    ASSERT_TRUE(wz::engine::assets::import_glb_meshes(
        bytes.data(),
        bytes.size(),
        options,
        out));

    ASSERT_FALSE(out.meshes.empty());

    const auto& mesh = out.meshes[0].mesh;

    EXPECT_TRUE(mesh.valid());
    EXPECT_EQ(mesh.topology, wz::engine::assets::MeshPrimitiveTopology::TriangleList);
    EXPECT_EQ(mesh.index_format, wz::engine::assets::MeshIndexFormat::UInt32);

    EXPECT_GT(mesh.vertex_count(), 0u);
    EXPECT_GT(mesh.index_count(), 0u);
    EXPECT_EQ(mesh.index_count() % 3u, 0u);
    EXPECT_TRUE(mesh.has_normals);

    for (const auto index : mesh.indices)
        EXPECT_LT(index, mesh.vertex_count());
}

TEST(GLTFImporter, ImportsLowPolyRockGLBAsMeshData)
{
    const auto path = fixture_path("gltf/low_poly_rock.glb");
    SCOPED_TRACE(path.string());

    const auto bytes = read_binary_file(path);
    ASSERT_FALSE(bytes.empty());

    wz::engine::assets::ImportedGLTFMeshSet out;
    wz::engine::assets::GLTFImportOptions options;

    ASSERT_TRUE(wz::engine::assets::import_glb_meshes(
        bytes.data(),
        bytes.size(),
        options,
        out));

    ASSERT_FALSE(out.meshes.empty());

    const auto& mesh = out.meshes[0].mesh;

    EXPECT_TRUE(mesh.valid());
    EXPECT_EQ(mesh.topology, wz::engine::assets::MeshPrimitiveTopology::TriangleList);
    EXPECT_EQ(mesh.index_format, wz::engine::assets::MeshIndexFormat::UInt32);

    EXPECT_GT(mesh.vertex_count(), 100u);
    EXPECT_GT(mesh.index_count(), 300u);
    EXPECT_EQ(mesh.index_count() % 3u, 0u);
    EXPECT_TRUE(mesh.has_normals);

    for (const auto index : mesh.indices)
        EXPECT_LT(index, mesh.vertex_count());
}

TEST(GLTFImporter, ImportsSceneHierarchyAndTransforms)
{
    const char* gltf = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [
    { "name": "tank_scene", "nodes": [0] }
  ],
  "nodes": [
    {
      "name": "tank_body",
      "translation": [1, 2, 3],
      "children": [1]
    },
    {
      "name": "turret",
      "translation": [0, 1, 0],
      "rotation": [0, 0, 0, 1],
      "scale": [2, 2, 2]
    }
  ]
})";

    wz::engine::assets::ImportedGLTFScene out;
    std::string error;

    ASSERT_TRUE(wz::engine::assets::import_gltf_scene(
        reinterpret_cast<const std::uint8_t*>(gltf),
        std::strlen(gltf),
        wz::engine::assets::GLTFSceneImportOptions{},
        out,
        &error)) << error;

    ASSERT_EQ(out.nodes.size(), 2u);
    EXPECT_EQ(out.name, "tank_scene");

    EXPECT_EQ(out.nodes[0].id, "tank_body");
    EXPECT_FALSE(out.nodes[0].parent_id.has_value());
    EXPECT_FLOAT_EQ(out.nodes[0].local.translation[0], 1.0f);
    EXPECT_FLOAT_EQ(out.nodes[0].local.translation[1], 2.0f);
    EXPECT_FLOAT_EQ(out.nodes[0].local.translation[2], 3.0f);

    EXPECT_EQ(out.nodes[1].id, "turret");
    ASSERT_TRUE(out.nodes[1].parent_id.has_value());
    EXPECT_EQ(*out.nodes[1].parent_id, "tank_body");
    EXPECT_FLOAT_EQ(out.nodes[1].local.translation[1], 1.0f);
    EXPECT_FLOAT_EQ(out.nodes[1].local.scale[0], 2.0f);
    EXPECT_FLOAT_EQ(out.nodes[1].local.scale[1], 2.0f);
    EXPECT_FLOAT_EQ(out.nodes[1].local.scale[2], 2.0f);
}

TEST(GLTFImporter, RejectsUnsupportedSceneMatrixTransform)
{
    const char* gltf = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [
    { "nodes": [0] }
  ],
  "nodes": [
    {
      "name": "sheared_node",
      "matrix": [
        1, 0, 0, 0,
        0.25, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
      ]
    }
  ]
})";

    wz::engine::assets::ImportedGLTFScene out;
    std::string error;

    EXPECT_FALSE(wz::engine::assets::import_gltf_scene(
        reinterpret_cast<const std::uint8_t*>(gltf),
        std::strlen(gltf),
        wz::engine::assets::GLTFSceneImportOptions{},
        out,
        &error));
    EXPECT_NE(error.find("sheared_node"), std::string::npos);
}

TEST(GLBMeshAsset, ImportsCubeThroughAssetGraph)
{
    wz::gpu::Device device{};
    wz::Logger logger{};

    wz::engine::assets::EngineAssetLibrary assets(
        device,
        logger,
        WZ_TEST_FIXTURE_DIR);

    const wz::asset::AssetKey file_key =
        assets.files().register_file_node(
            "gltf/cube.glb",
            wz::engine::assets::kRawFileSchema,
            wz::engine::assets::kAssetTypeRawFile);

    ASSERT_FALSE(file_key == wz::asset::AssetKey{});

    auto mesh = assets.meshes().create_glb_mesh({
        .name = "cube",
        .source_file = file_key,
        .mesh_index = 0,
    });

    ASSERT_TRUE(mesh.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto handle = assets.meshes().get_mesh(mesh);
    ASSERT_TRUE(handle.valid());

    const auto* data = assets.meshes().get_mesh_data(handle);
    ASSERT_NE(data, nullptr);

    EXPECT_TRUE(data->valid());
    EXPECT_GT(data->vertex_count(), 0u);
    EXPECT_GT(data->index_count(), 0u);
    EXPECT_EQ(data->index_count() % 3u, 0u);

    for (const auto index : data->indices)
        EXPECT_LT(index, data->vertex_count());
}

TEST(GLBMeshAsset, ImportsCubeThroughAssetGraphFromAbsolutePath)
{
    wz::gpu::Device device{};
    wz::Logger logger{};

    wz::engine::assets::EngineAssetLibrary assets(
        device,
        logger,
        WZ_TEST_FIXTURE_DIR);

    const wz::asset::AssetKey file_key =
        assets.files().register_file_node(
            wz::fs::join(WZ_TEST_FIXTURE_DIR, "gltf/cube.glb"),
            wz::engine::assets::kRawFileSchema,
            wz::engine::assets::kAssetTypeRawFile);

    ASSERT_FALSE(file_key == wz::asset::AssetKey{});

    auto mesh = assets.meshes().create_glb_mesh({
        .name = "cube_absolute_path",
        .source_file = file_key,
        .mesh_index = 0,
    });

    ASSERT_TRUE(mesh.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto handle = assets.meshes().get_mesh(mesh);
    ASSERT_TRUE(handle.valid());

    const auto* data = assets.meshes().get_mesh_data(handle);
    ASSERT_NE(data, nullptr);

    EXPECT_TRUE(data->valid());
    EXPECT_GT(data->vertex_count(), 0u);
    EXPECT_GT(data->index_count(), 0u);
    EXPECT_EQ(data->index_count() % 3u, 0u);
}

TEST(GLBMeshAsset, UsesProjectDiskCache)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_glb_mesh_disk_cache_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const wz::fs::Path cache_root =
        wz::fs::join(root, ".wozzits/cache");

    wz::gpu::Device device{};
    wz::Logger logger{};

    auto resolve_mesh = [&]() -> wz::engine::assets::MeshData
    {
        wz::engine::assets::EngineAssetLibrary assets{
            device,
            logger,
            WZ_TEST_FIXTURE_DIR,
            wz::engine::assets::EngineAssetCacheSettings{
                .root = cache_root,
                .enabled = true,
            },
        };

        const wz::asset::AssetKey file_key =
            assets.files().register_file_node(
                "gltf/cube.glb",
                wz::engine::assets::kRawFileSchema,
                wz::engine::assets::kAssetTypeRawFile);
        EXPECT_FALSE(file_key == wz::asset::AssetKey{});

        const auto mesh = assets.meshes().create_glb_mesh({
            .name = "cube_cached",
            .source_file = file_key,
            .mesh_index = 0,
        });
        EXPECT_TRUE(mesh.valid());

        EXPECT_TRUE(assets.commit());
        const auto report = assets.resolve_all();
        EXPECT_TRUE(report.ok());

        const auto handle = assets.meshes().get_mesh(mesh);
        EXPECT_TRUE(handle.valid());
        const auto* data = assets.meshes().get_mesh_data(handle);
        EXPECT_NE(data, nullptr);
        return data ? *data : wz::engine::assets::MeshData{};
    };

    const wz::engine::assets::MeshData first = resolve_mesh();
    ASSERT_TRUE(first.valid());

    const wz::fs::Path cache_directory =
        wz::fs::join(wz::fs::join(cache_root, "assets"), "glb_mesh");
    const auto entries = wz::fs::list_directory(cache_directory);
    ASSERT_EQ(entries.error, wz::fs::FileError::None);
    EXPECT_FALSE(entries.value.empty());

    const wz::engine::assets::MeshData second = resolve_mesh();
    ASSERT_TRUE(second.valid());
    EXPECT_EQ(second.vertex_count(), first.vertex_count());
    EXPECT_EQ(second.index_count(), first.index_count());
    EXPECT_EQ(second.has_normals, first.has_normals);
    EXPECT_EQ(second.has_uv0, first.has_uv0);
}
