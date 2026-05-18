// tests/asset_gaussian_splat/ply_importer.cpp

#include <gtest/gtest.h>

#include <engine/assets/gaussian_splat/gaussian_splat_ply_importer.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    std::filesystem::path write_temp_ply(
        const std::string& filename,
        const std::string& text)
    {
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / filename;

        std::ofstream out(path, std::ios::binary);
        EXPECT_TRUE(out.good());

        out << text;
        out.close();

        return path;
    }
}

TEST(GaussianSplatPLYImporter, ImportsTwoSplatAsciiPLYFromDisk)
{
    const std::string text =
        "ply\n"
        "format ascii 1.0\n"
        "element vertex 2\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "property float f_dc_0\n"
        "property float f_dc_1\n"
        "property float f_dc_2\n"
        "property float opacity\n"
        "property float scale_0\n"
        "property float scale_1\n"
        "property float scale_2\n"
        "property float rot_0\n"
        "property float rot_1\n"
        "property float rot_2\n"
        "property float rot_3\n"
        "end_header\n"
        "1 2 3 0.1 0.2 0.3 -2.0 -1.0 -1.1 -1.2 1 0 0 0\n"
        "4 5 6 0.4 0.5 0.6 -3.0 -2.0 -2.1 -2.2 0 1 0 0\n";

    const std::filesystem::path path =
        write_temp_ply("wozzits_import_two_splats_ascii.ply", text);

    const wz::engine::assets::GaussianSplatImportResult imported =
        wz::engine::assets::import_gaussian_splat_ply_file(path);

    ASSERT_TRUE(imported.ok) << imported.error;

    const wz::engine::assets::GaussianSplatCloudData& cloud = imported.cloud;

    ASSERT_EQ(cloud.splats.size(), 2u);
    EXPECT_EQ(cloud.splat_count(), 2u);
    EXPECT_FALSE(cloud.empty());

    EXPECT_TRUE(cloud.bounds.valid);

    EXPECT_FLOAT_EQ(cloud.bounds.min[0], 1.0f);
    EXPECT_FLOAT_EQ(cloud.bounds.min[1], 2.0f);
    EXPECT_FLOAT_EQ(cloud.bounds.min[2], 3.0f);

    EXPECT_FLOAT_EQ(cloud.bounds.max[0], 4.0f);
    EXPECT_FLOAT_EQ(cloud.bounds.max[1], 5.0f);
    EXPECT_FLOAT_EQ(cloud.bounds.max[2], 6.0f);

    EXPECT_FLOAT_EQ(cloud.opacity_min, -3.0f);
    EXPECT_FLOAT_EQ(cloud.opacity_max, -2.0f);

    EXPECT_FLOAT_EQ(cloud.scale_min, -2.2f);
    EXPECT_FLOAT_EQ(cloud.scale_max, -1.0f);

    EXPECT_EQ(cloud.f_rest_count, 0u);

    const wz::engine::assets::GaussianSplat& a = cloud.splats[0];

    EXPECT_FLOAT_EQ(a.position[0], 1.0f);
    EXPECT_FLOAT_EQ(a.position[1], 2.0f);
    EXPECT_FLOAT_EQ(a.position[2], 3.0f);

    EXPECT_FLOAT_EQ(a.opacity, -2.0f);

    EXPECT_FLOAT_EQ(a.scale[0], -1.0f);
    EXPECT_FLOAT_EQ(a.scale[1], -1.1f);
    EXPECT_FLOAT_EQ(a.scale[2], -1.2f);

    EXPECT_FLOAT_EQ(a.rotation[0], 1.0f);
    EXPECT_FLOAT_EQ(a.rotation[1], 0.0f);
    EXPECT_FLOAT_EQ(a.rotation[2], 0.0f);
    EXPECT_FLOAT_EQ(a.rotation[3], 0.0f);

    EXPECT_FLOAT_EQ(a.color_dc[0], 0.1f);
    EXPECT_FLOAT_EQ(a.color_dc[1], 0.2f);
    EXPECT_FLOAT_EQ(a.color_dc[2], 0.3f);

    EXPECT_TRUE(a.color_rest.empty());

    const wz::engine::assets::GaussianSplat& b = cloud.splats[1];

    EXPECT_FLOAT_EQ(b.position[0], 4.0f);
    EXPECT_FLOAT_EQ(b.position[1], 5.0f);
    EXPECT_FLOAT_EQ(b.position[2], 6.0f);

    EXPECT_FLOAT_EQ(b.opacity, -3.0f);

    EXPECT_FLOAT_EQ(b.scale[0], -2.0f);
    EXPECT_FLOAT_EQ(b.scale[1], -2.1f);
    EXPECT_FLOAT_EQ(b.scale[2], -2.2f);

    EXPECT_FLOAT_EQ(b.rotation[0], 0.0f);
    EXPECT_FLOAT_EQ(b.rotation[1], 1.0f);
    EXPECT_FLOAT_EQ(b.rotation[2], 0.0f);
    EXPECT_FLOAT_EQ(b.rotation[3], 0.0f);

    EXPECT_FLOAT_EQ(b.color_dc[0], 0.4f);
    EXPECT_FLOAT_EQ(b.color_dc[1], 0.5f);
    EXPECT_FLOAT_EQ(b.color_dc[2], 0.6f);

    EXPECT_TRUE(b.color_rest.empty());

    std::filesystem::remove(path);
}

TEST(GaussianSplatPLYImporter, ImportsFRestValuesInSchemaOrder)
{
    const std::string text =
        "ply\n"
        "format ascii 1.0\n"
        "element vertex 1\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "property float f_dc_0\n"
        "property float f_dc_1\n"
        "property float f_dc_2\n"
        "property float opacity\n"
        "property float scale_0\n"
        "property float scale_1\n"
        "property float scale_2\n"
        "property float rot_0\n"
        "property float rot_1\n"
        "property float rot_2\n"
        "property float rot_3\n"
        "property float f_rest_10\n"
        "property float f_rest_2\n"
        "property float f_rest_0\n"
        "property float f_rest_1\n"
        "end_header\n"
        "1 2 3 0.1 0.2 0.3 -2.0 -1.0 -1.1 -1.2 1 0 0 0 10 2 0 1\n";

    const std::filesystem::path path =
        write_temp_ply("wozzits_import_f_rest_order.ply", text);

    const wz::engine::assets::GaussianSplatImportResult imported =
        wz::engine::assets::import_gaussian_splat_ply_file(path);

    ASSERT_TRUE(imported.ok) << imported.error;

    ASSERT_EQ(imported.cloud.splats.size(), 1u);
    EXPECT_EQ(imported.cloud.f_rest_count, 4u);

    const wz::engine::assets::GaussianSplat& splat =
        imported.cloud.splats[0];

    ASSERT_EQ(splat.color_rest.size(), 4u);

    // Numeric schema order:
    // f_rest_0, f_rest_1, f_rest_2, f_rest_10
    EXPECT_FLOAT_EQ(splat.color_rest[0], 0.0f);
    EXPECT_FLOAT_EQ(splat.color_rest[1], 1.0f);
    EXPECT_FLOAT_EQ(splat.color_rest[2], 2.0f);
    EXPECT_FLOAT_EQ(splat.color_rest[3], 10.0f);

    std::filesystem::remove(path);
}

TEST(GaussianSplatPLYImporter, RejectsOrdinaryMeshPLYFromDisk)
{
    const std::string text =
        "ply\n"
        "format ascii 1.0\n"
        "element vertex 1\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "property float nx\n"
        "property float ny\n"
        "property float nz\n"
        "end_header\n"
        "0 0 0 0 1 0\n";

    const std::filesystem::path path =
        write_temp_ply("wozzits_import_mesh_not_splat.ply", text);

    const wz::engine::assets::GaussianSplatImportResult imported =
        wz::engine::assets::import_gaussian_splat_ply_file(path);

    EXPECT_FALSE(imported.ok);
    EXPECT_FALSE(imported.error.empty());
    EXPECT_NE(imported.error.find("opacity"), std::string::npos);

    std::filesystem::remove(path);
}

TEST(GaussianSplatPLYImporter, ReportsMissingFile)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        "wozzits_missing_gaussian_splat_import_file_12345.ply";

    std::filesystem::remove(path);

    const wz::engine::assets::GaussianSplatImportResult imported =
        wz::engine::assets::import_gaussian_splat_ply_file(path);

    EXPECT_FALSE(imported.ok);
    EXPECT_FALSE(imported.error.empty());
}