// file: tests/asset/gltf_importer.cpp

#include <gtest/gtest.h>

#include <engine/assets/gltf/gltf_importer.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

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
    const auto path = fixture_path("gltf/cube.glb");
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

    for (const auto index : mesh.indices)
        EXPECT_LT(index, mesh.vertex_count());
}