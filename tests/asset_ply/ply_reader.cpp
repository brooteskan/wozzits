// tests/external_ply/external_ply_reader.cpp

#include <gtest/gtest.h>

#include <ply/ply_reader.h>

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