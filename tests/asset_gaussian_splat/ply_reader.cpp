// tests/external_ply/external_ply_reader.cpp

#include <gtest/gtest.h>

#include <ply/ply_reader.h>
#include <engine/assets/gaussian_splat/gaussian_splat_ply_schema.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    std::filesystem::path write_temp_ply(const std::string& filename, const std::string& text)
    {
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / filename;

        std::ofstream out(path, std::ios::binary);
        EXPECT_TRUE(out.good());

        out << text;
        out.close();

        return path;
    }

    const wz::external::ply::ScalarTable* find_table(
        const wz::external::ply::Document& document,
        const std::string& name)
    {
        for (const wz::external::ply::ScalarTable& table : document.scalar_tables)
        {
            if (table.element_name == name)
            {
                return &table;
            }
        }

        return nullptr;
    }

    int find_property_index(
        const wz::external::ply::ScalarTable& table,
        const std::string& name)
    {
        for (size_t i = 0; i < table.properties.size(); ++i)
        {
            if (table.properties[i].name == name)
            {
                return static_cast<int>(i);
            }
        }

        return -1;
    }

    double value_at(
        const wz::external::ply::ScalarTable& table,
        size_t row,
        size_t property_index)
    {
        return table.values[row * table.properties.size() + property_index];
    }
}

TEST(ExternalPLYReader, ReadsMinimalAsciiVertexPLY)
{
    const std::string text =
        "ply\n"
        "format ascii 1.0\n"
        "element vertex 2\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "end_header\n"
        "0 1 2\n"
        "3 4 5\n";

    const std::filesystem::path path =
        write_temp_ply("wozzits_minimal_ascii_vertex.ply", text);

    const wz::external::ply::ReadResult result =
        wz::external::ply::read_ply_file(path);

    ASSERT_TRUE(result.ok) << result.error.message;

    ASSERT_EQ(result.document.header.elements.size(), 1u);
    EXPECT_EQ(result.document.header.elements[0].name, "vertex");
    EXPECT_EQ(result.document.header.elements[0].count, 2u);
    EXPECT_EQ(result.document.header.elements[0].properties.size(), 3u);

    const wz::external::ply::ScalarTable* table =
        find_table(result.document, "vertex");

    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->row_count, 2u);
    ASSERT_EQ(table->properties.size(), 3u);

    EXPECT_EQ(table->properties[0].name, "x");
    EXPECT_EQ(table->properties[1].name, "y");
    EXPECT_EQ(table->properties[2].name, "z");

    ASSERT_EQ(table->values.size(), 6u);

    EXPECT_DOUBLE_EQ(value_at(*table, 0, 0), 0.0);
    EXPECT_DOUBLE_EQ(value_at(*table, 0, 1), 1.0);
    EXPECT_DOUBLE_EQ(value_at(*table, 0, 2), 2.0);

    EXPECT_DOUBLE_EQ(value_at(*table, 1, 0), 3.0);
    EXPECT_DOUBLE_EQ(value_at(*table, 1, 1), 4.0);
    EXPECT_DOUBLE_EQ(value_at(*table, 1, 2), 5.0);

    std::filesystem::remove(path);
}

TEST(ExternalPLYReader, PreservesGaussianSplatStylePropertyNames)
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
        "end_header\n"
        "1 2 3 0.1 0.2 0.3 -2.0 -1.0 -1.1 -1.2 1 0 0 0\n";

    const std::filesystem::path path =
        write_temp_ply("wozzits_gaussian_splat_style_ascii.ply", text);

    const wz::external::ply::ReadResult result =
        wz::external::ply::read_ply_file(path);

    ASSERT_TRUE(result.ok) << result.error.message;

    const wz::external::ply::ScalarTable* table =
        find_table(result.document, "vertex");

    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->row_count, 1u);

    const std::vector<std::string> expected_properties = {
        "x",
        "y",
        "z",
        "f_dc_0",
        "f_dc_1",
        "f_dc_2",
        "opacity",
        "scale_0",
        "scale_1",
        "scale_2",
        "rot_0",
        "rot_1",
        "rot_2",
        "rot_3",
    };

    ASSERT_EQ(table->properties.size(), expected_properties.size());

    for (size_t i = 0; i < expected_properties.size(); ++i)
    {
        EXPECT_EQ(table->properties[i].name, expected_properties[i]);
    }

    EXPECT_GE(find_property_index(*table, "opacity"), 0);
    EXPECT_GE(find_property_index(*table, "scale_0"), 0);
    EXPECT_GE(find_property_index(*table, "rot_3"), 0);
    EXPECT_GE(find_property_index(*table, "f_dc_2"), 0);

    std::filesystem::remove(path);
}

TEST(ExternalPLYReader, SkipsFaceListPropertiesButReadsVertexScalars)
{
    const std::string text =
        "ply\n"
        "format ascii 1.0\n"
        "element vertex 3\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "element face 1\n"
        "property list uchar int vertex_indices\n"
        "end_header\n"
        "0 0 0\n"
        "1 0 0\n"
        "0 1 0\n"
        "3 0 1 2\n";

    const std::filesystem::path path =
        write_temp_ply("wozzits_face_list_ascii.ply", text);

    const wz::external::ply::ReadResult result =
        wz::external::ply::read_ply_file(path);

    ASSERT_TRUE(result.ok) << result.error.message;

    ASSERT_EQ(result.document.header.elements.size(), 2u);
    EXPECT_EQ(result.document.header.elements[0].name, "vertex");
    EXPECT_EQ(result.document.header.elements[1].name, "face");

    ASSERT_EQ(result.document.header.elements[1].properties.size(), 1u);
    EXPECT_TRUE(result.document.header.elements[1].properties[0].is_list);

    const wz::external::ply::ScalarTable* vertex_table =
        find_table(result.document, "vertex");

    ASSERT_NE(vertex_table, nullptr);
    EXPECT_EQ(vertex_table->row_count, 3u);
    ASSERT_EQ(vertex_table->properties.size(), 3u);
    ASSERT_EQ(vertex_table->values.size(), 9u);

    // We should not create a scalar table for face, because the only face
    // property is a list property.
    const wz::external::ply::ScalarTable* face_table =
        find_table(result.document, "face");

    EXPECT_EQ(face_table, nullptr);

    std::filesystem::remove(path);
}

TEST(ExternalPLYReader, ReportsMissingFile)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "wozzits_this_file_should_not_exist_12345.ply";

    std::filesystem::remove(path);

    const wz::external::ply::ReadResult result =
        wz::external::ply::read_ply_file(path);

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.message.empty());
}

TEST(ExternalPLYReader, ReadsTinyplyBinaryIcosahedronFixture)
{
    const std::filesystem::path path =
        std::filesystem::path(WZ_TEST_FIXTURE_DIR) /
        "splats" /
        "icosahedron.ply";

    const wz::external::ply::ReadResult result =
        wz::external::ply::read_ply_file(path);

    ASSERT_TRUE(result.ok) << result.error.message;

    ASSERT_EQ(result.document.header.elements.size(), 2u);

    EXPECT_EQ(result.document.header.elements[0].name, "vertex");
    EXPECT_EQ(result.document.header.elements[0].count, 12u);
    ASSERT_EQ(result.document.header.elements[0].properties.size(), 6u);

    EXPECT_EQ(result.document.header.elements[0].properties[0].name, "x");
    EXPECT_EQ(result.document.header.elements[0].properties[1].name, "y");
    EXPECT_EQ(result.document.header.elements[0].properties[2].name, "z");
    EXPECT_EQ(result.document.header.elements[0].properties[3].name, "nx");
    EXPECT_EQ(result.document.header.elements[0].properties[4].name, "ny");
    EXPECT_EQ(result.document.header.elements[0].properties[5].name, "nz");

    EXPECT_EQ(result.document.header.elements[1].name, "face");
    EXPECT_EQ(result.document.header.elements[1].count, 20u);
    ASSERT_EQ(result.document.header.elements[1].properties.size(), 1u);

    EXPECT_EQ(result.document.header.elements[1].properties[0].name, "vertex_indices");
    EXPECT_TRUE(result.document.header.elements[1].properties[0].is_list);

    const wz::external::ply::ScalarTable* vertex_table =
        find_table(result.document, "vertex");

    ASSERT_NE(vertex_table, nullptr);
    EXPECT_EQ(vertex_table->row_count, 12u);
    ASSERT_EQ(vertex_table->properties.size(), 6u);
    ASSERT_EQ(vertex_table->values.size(), 12u * 6u);

    EXPECT_DOUBLE_EQ(value_at(*vertex_table, 0, 0), 0.0);
    EXPECT_DOUBLE_EQ(value_at(*vertex_table, 0, 1), 0.0);
    EXPECT_DOUBLE_EQ(value_at(*vertex_table, 0, 2), -1.0);

    EXPECT_NEAR(value_at(*vertex_table, 0, 3), 3.69549e-06, 1e-5);
    EXPECT_DOUBLE_EQ(value_at(*vertex_table, 0, 4), 0.0);
    EXPECT_NEAR(value_at(*vertex_table, 0, 5), -4.16078, 1e-5);

    // Face has only a list property, so the v1 scalar-table wrapper should skip it.
    const wz::external::ply::ScalarTable* face_table =
        find_table(result.document, "face");

    EXPECT_EQ(face_table, nullptr);
}

TEST(ExternalPLYReader, ReadsTinyplyASCIIIcosahedronFixture)
{
    const std::filesystem::path path =
        std::filesystem::path(WZ_TEST_FIXTURE_DIR) /
        "splats" /
        "icosahedron_ascii.ply";

    const wz::external::ply::ReadResult result =
        wz::external::ply::read_ply_file(path);

    ASSERT_TRUE(result.ok) << result.error.message;

    ASSERT_EQ(result.document.header.elements.size(), 2u);

    EXPECT_EQ(result.document.header.elements[0].name, "vertex");
    EXPECT_EQ(result.document.header.elements[0].count, 12u);
    ASSERT_EQ(result.document.header.elements[0].properties.size(), 6u);

    EXPECT_EQ(result.document.header.elements[0].properties[0].name, "x");
    EXPECT_EQ(result.document.header.elements[0].properties[1].name, "y");
    EXPECT_EQ(result.document.header.elements[0].properties[2].name, "z");
    EXPECT_EQ(result.document.header.elements[0].properties[3].name, "nx");
    EXPECT_EQ(result.document.header.elements[0].properties[4].name, "ny");
    EXPECT_EQ(result.document.header.elements[0].properties[5].name, "nz");

    EXPECT_EQ(result.document.header.elements[1].name, "face");
    EXPECT_EQ(result.document.header.elements[1].count, 20u);
    ASSERT_EQ(result.document.header.elements[1].properties.size(), 1u);

    EXPECT_EQ(result.document.header.elements[1].properties[0].name, "vertex_indices");
    EXPECT_TRUE(result.document.header.elements[1].properties[0].is_list);

    const wz::external::ply::ScalarTable* vertex_table =
        find_table(result.document, "vertex");

    ASSERT_NE(vertex_table, nullptr);
    EXPECT_EQ(vertex_table->row_count, 12u);
    ASSERT_EQ(vertex_table->properties.size(), 6u);
    ASSERT_EQ(vertex_table->values.size(), 12u * 6u);

    EXPECT_DOUBLE_EQ(value_at(*vertex_table, 0, 0), 0.0);
    EXPECT_DOUBLE_EQ(value_at(*vertex_table, 0, 1), 0.0);
    EXPECT_DOUBLE_EQ(value_at(*vertex_table, 0, 2), -1.0);

    EXPECT_NEAR(value_at(*vertex_table, 0, 3), 3.69549e-06, 1e-10);
    EXPECT_DOUBLE_EQ(value_at(*vertex_table, 0, 4), 0.0);
    EXPECT_NEAR(value_at(*vertex_table, 0, 5), -4.16078, 1e-6);

    // Face has only a list property, so the v1 scalar-table wrapper should skip it.
    const wz::external::ply::ScalarTable* face_table =
        find_table(result.document, "face");

    EXPECT_EQ(face_table, nullptr);
}

TEST(GaussianSplatPLYSchema, DetectsRequiredGaussianSplatFields)
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
        "end_header\n"
        "1 2 3 0.1 0.2 0.3 -2.0 -1.0 -1.1 -1.2 1 0 0 0\n";

    const std::filesystem::path path =
        write_temp_ply("wozzits_schema_valid_splat.ply", text);

    const wz::external::ply::ReadResult read =
        wz::external::ply::read_ply_file(path);

    ASSERT_TRUE(read.ok) << read.error.message;

    const wz::external::ply::ScalarTable* table =
        find_table(read.document, "vertex");

    ASSERT_NE(table, nullptr);

    const wz::engine::assets::GaussianSplatPLYSchemaResult detected =
        wz::engine::assets::detect_gaussian_splat_ply_schema(*table);

    ASSERT_TRUE(detected.ok) << detected.error;

    EXPECT_TRUE(detected.schema.has_required_fields());

    EXPECT_EQ(detected.schema.x, 0);
    EXPECT_EQ(detected.schema.y, 1);
    EXPECT_EQ(detected.schema.z, 2);

    EXPECT_EQ(detected.schema.f_dc_0, 3);
    EXPECT_EQ(detected.schema.f_dc_1, 4);
    EXPECT_EQ(detected.schema.f_dc_2, 5);

    EXPECT_EQ(detected.schema.opacity, 6);

    EXPECT_EQ(detected.schema.scale_0, 7);
    EXPECT_EQ(detected.schema.scale_1, 8);
    EXPECT_EQ(detected.schema.scale_2, 9);

    EXPECT_EQ(detected.schema.rot_0, 10);
    EXPECT_EQ(detected.schema.rot_1, 11);
    EXPECT_EQ(detected.schema.rot_2, 12);
    EXPECT_EQ(detected.schema.rot_3, 13);

    EXPECT_TRUE(detected.schema.f_rest.empty());

    std::filesystem::remove(path);
}

TEST(GaussianSplatPLYSchema, RejectsOrdinaryMeshPLY)
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
        write_temp_ply("wozzits_schema_mesh_not_splat.ply", text);

    const wz::external::ply::ReadResult read =
        wz::external::ply::read_ply_file(path);

    ASSERT_TRUE(read.ok) << read.error.message;

    const wz::external::ply::ScalarTable* table =
        find_table(read.document, "vertex");

    ASSERT_NE(table, nullptr);

    const wz::engine::assets::GaussianSplatPLYSchemaResult detected =
        wz::engine::assets::detect_gaussian_splat_ply_schema(*table);

    EXPECT_FALSE(detected.ok);
    EXPECT_FALSE(detected.error.empty());

    EXPECT_NE(detected.error.find("opacity"), std::string::npos);
    EXPECT_NE(detected.error.find("scale_0"), std::string::npos);
    EXPECT_NE(detected.error.find("rot_0"), std::string::npos);
    EXPECT_NE(detected.error.find("f_dc_0"), std::string::npos);

    std::filesystem::remove(path);
}

TEST(GaussianSplatPLYSchema, ReportsMissingOpacity)
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
        "property float scale_0\n"
        "property float scale_1\n"
        "property float scale_2\n"
        "property float rot_0\n"
        "property float rot_1\n"
        "property float rot_2\n"
        "property float rot_3\n"
        "end_header\n"
        "1 2 3 0.1 0.2 0.3 -1.0 -1.1 -1.2 1 0 0 0\n";

    const std::filesystem::path path =
        write_temp_ply("wozzits_schema_missing_opacity.ply", text);

    const wz::external::ply::ReadResult read =
        wz::external::ply::read_ply_file(path);

    ASSERT_TRUE(read.ok) << read.error.message;

    const wz::external::ply::ScalarTable* table =
        find_table(read.document, "vertex");

    ASSERT_NE(table, nullptr);

    const wz::engine::assets::GaussianSplatPLYSchemaResult detected =
        wz::engine::assets::detect_gaussian_splat_ply_schema(*table);

    EXPECT_FALSE(detected.ok);
    EXPECT_NE(detected.error.find("opacity"), std::string::npos);

    std::filesystem::remove(path);
}

TEST(GaussianSplatPLYSchema, CollectsFRestPropertiesInNumericOrder)
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
        write_temp_ply("wozzits_schema_f_rest_order.ply", text);

    const wz::external::ply::ReadResult read =
        wz::external::ply::read_ply_file(path);

    ASSERT_TRUE(read.ok) << read.error.message;

    const wz::external::ply::ScalarTable* table =
        find_table(read.document, "vertex");

    ASSERT_NE(table, nullptr);

    const wz::engine::assets::GaussianSplatPLYSchemaResult detected =
        wz::engine::assets::detect_gaussian_splat_ply_schema(*table);

    ASSERT_TRUE(detected.ok) << detected.error;

    ASSERT_EQ(detected.schema.f_rest.size(), 4u);

    // Property declaration order:
    // index 14 = f_rest_10
    // index 15 = f_rest_2
    // index 16 = f_rest_0
    // index 17 = f_rest_1
    //
    // Numeric order should be:
    // f_rest_0, f_rest_1, f_rest_2, f_rest_10
    EXPECT_EQ(detected.schema.f_rest[0], 16);
    EXPECT_EQ(detected.schema.f_rest[1], 17);
    EXPECT_EQ(detected.schema.f_rest[2], 15);
    EXPECT_EQ(detected.schema.f_rest[3], 14);

    std::filesystem::remove(path);
}

TEST(GaussianSplatPLYSchema, IgnoresUnknownExtraProperties)
{
    const std::string text =
        "ply\n"
        "format ascii 1.0\n"
        "element vertex 1\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "property float weird_tool_specific_value\n"
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
        "1 2 3 999 0.1 0.2 0.3 -2.0 -1.0 -1.1 -1.2 1 0 0 0\n";

    const std::filesystem::path path =
        write_temp_ply("wozzits_schema_extra_properties.ply", text);

    const wz::external::ply::ReadResult read =
        wz::external::ply::read_ply_file(path);

    ASSERT_TRUE(read.ok) << read.error.message;

    const wz::external::ply::ScalarTable* table =
        find_table(read.document, "vertex");

    ASSERT_NE(table, nullptr);

    const wz::engine::assets::GaussianSplatPLYSchemaResult detected =
        wz::engine::assets::detect_gaussian_splat_ply_schema(*table);

    ASSERT_TRUE(detected.ok) << detected.error;

    EXPECT_EQ(detected.schema.x, 0);
    EXPECT_EQ(detected.schema.y, 1);
    EXPECT_EQ(detected.schema.z, 2);

    EXPECT_EQ(detected.schema.f_dc_0, 4);
    EXPECT_EQ(detected.schema.opacity, 7);

    std::filesystem::remove(path);
}