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

TEST(GaussianSplatPLYSchema, AcceptsOrdinaryMeshPLYWithPositionDefaults)
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

    ASSERT_TRUE(detected.ok) << detected.error;
    EXPECT_TRUE(detected.schema.has_required_fields());
    EXPECT_EQ(detected.schema.x, 0);
    EXPECT_EQ(detected.schema.y, 1);
    EXPECT_EQ(detected.schema.z, 2);
    EXPECT_EQ(detected.schema.opacity, -1);
    EXPECT_EQ(detected.schema.scale_0, -1);
    EXPECT_EQ(detected.schema.rot_0, -1);
    EXPECT_EQ(detected.schema.f_dc_0, -1);

    std::filesystem::remove(path);
}

TEST(GaussianSplatPLYSchema, MissingOpacityUsesImportDefault)
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

    ASSERT_TRUE(detected.ok) << detected.error;
    EXPECT_TRUE(detected.schema.has_required_fields());
    EXPECT_EQ(detected.schema.opacity, -1);
    EXPECT_EQ(detected.schema.scale_0, 6);
    EXPECT_EQ(detected.schema.rot_0, 9);
    EXPECT_EQ(detected.schema.f_dc_0, 3);

    std::filesystem::remove(path);
}

TEST(GaussianSplatPLYSchema, AcceptsPositionOnlyPLY)
{
    const std::string text =
        "ply\n"
        "format ascii 1.0\n"
        "element vertex 1\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "end_header\n"
        "0 1 2\n";

    const std::filesystem::path path =
        write_temp_ply("wozzits_schema_position_only_permissive.ply", text);

    const wz::external::ply::ReadResult read =
        wz::external::ply::read_ply_file(path);

    ASSERT_TRUE(read.ok) << read.error.message;

    const wz::external::ply::ScalarTable* table =
        find_table(read.document, "vertex");

    ASSERT_NE(table, nullptr);

    const wz::engine::assets::GaussianSplatPLYSchemaResult detected =
        wz::engine::assets::detect_gaussian_splat_ply_schema(*table);

    EXPECT_TRUE(detected.ok) << detected.error;
    EXPECT_EQ(detected.schema.x, 0);
    EXPECT_EQ(detected.schema.y, 1);
    EXPECT_EQ(detected.schema.z, 2);
    EXPECT_TRUE(detected.schema.has_required_fields());

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

// ---------------------------------------------------------------------------
// Hostile PLY headers (issue #310, A4-C2 / A4-C3 / A4-C4).
//
// These guards live in the wz_ply WRAPPER rather than in vendored tinyply, and
// they therefore protect BOTH entry points into this library -- the
// gaussian-splat importer and the star-catalog importer both arrive through
// read_ply_bytes.
//
// Every case below was MEASURED against the real importer before the fix:
//   wrapping count  -> ACCESS_VIOLATION      (0xC0000005) from 145 bytes
//   colliding key   -> HEAP CORRUPTION       (0xC0000374) from 1151 bytes
//   unknown type    -> ok=1 with SILENTLY SHIFTED rows, no error, no log
//
// So the regression signal for the first two is a CRASHED TEST RUNNER, not a
// red assertion. Keep them binary: the count guard is deliberately weaker for
// ASCII payloads (one byte per row, because "0 0 0\n" is six bytes for three
// doubles whose binary stride is 24), so an ASCII version of the first test
// would not exercise the same bound.
// ---------------------------------------------------------------------------

namespace
{
    std::vector<std::uint8_t> ply_bytes(const std::string& header,
                                        const std::vector<std::uint8_t>& payload)
    {
        std::vector<std::uint8_t> bytes(header.begin(), header.end());
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        return bytes;
    }
}

TEST(ExternalPLYReader, RejectsElementCountThatCannotFitThePayload)
{
    // 2^62 + 1 with a 12-byte binary stride: count * 12 wraps to exactly 12, so
    // tinyply would allocate 12 bytes, read the 12 bytes present without
    // tripping its EOF guard, and then scatter 2^62 rows out of them.
    const std::string header =
        "ply\nformat binary_little_endian 1.0\n"
        "element vertex 4611686018427387905\n"
        "property float x\nproperty float y\nproperty float z\n"
        "end_header\n";

    const std::vector<std::uint8_t> payload(12u, 0u);
    const auto bytes = ply_bytes(header, payload);

    const auto result = wz::external::ply::read_ply_bytes(bytes);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.message.empty());
}

TEST(ExternalPLYReader, RejectsWrappingElementCountWithNoPayloadAtAll)
{
    // 2^62 exactly: BOTH tinyply products wrap to zero, giving a zero-length
    // bulk buffer that was then indexed from row 0 -- a fault from a file with
    // no payload bytes whatsoever.
    const std::string header =
        "ply\nformat binary_little_endian 1.0\n"
        "element vertex 4611686018427387904\n"
        "property float x\nproperty float y\nproperty float z\n"
        "end_header\n";

    const auto result = wz::external::ply::read_ply_bytes(ply_bytes(header, {}));
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.message.empty());
}

TEST(ExternalPLYReader, RejectsHeaderWhoseElementPropertyKeysCollide)
{
    // "vertex" + "x" == "verte" + "xx" under tinyply's unseparated buffer key.
    // The second is a LIST property specifically to slip past tinyply's own
    // duplicate-request guard, which this wrapper never triggers because it
    // skips list properties at request time.
    const std::string header =
        "ply\nformat binary_little_endian 1.0\n"
        "element vertex 1\n"
        "property float x\n"
        "element verte 1\n"
        "property list uchar float xx\n"
        "end_header\n";

    std::vector<std::uint8_t> payload(4u, 0u);   // vertex.x
    payload.push_back(0xFFu);                    // list count = 255
    payload.insert(payload.end(), 255u * 4u, 0x41u);

    const auto result = wz::external::ply::read_ply_bytes(ply_bytes(header, payload));
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.message.empty());
}

TEST(ExternalPLYReader, RejectsUnknownPropertyTypeInsteadOfDesyncingTheStream)
{
    // `half` is not a tinyply type, so it became Type::INVALID with stride 0.
    // The four real bytes of `w` were never consumed and every later field
    // shifted by four -- silently, with ok=1. Row 0 read correctly and row 1
    // read (50, 11, 21) instead of (11, 21, 31), which is exactly the kind of
    // wrongness no assertion downstream would think to question.
    const std::string header =
        "ply\nformat binary_little_endian 1.0\n"
        "element vertex 2\n"
        "property float x\nproperty float y\nproperty float z\n"
        "property half w\n"
        "property float nx\n"
        "end_header\n";

    const std::vector<std::uint8_t> payload(2u * 5u * 4u, 0u);

    const auto result = wz::external::ply::read_ply_bytes(ply_bytes(header, payload));
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.message.empty());
}

// The guards must not cost us well-formed files: a binary PLY whose count and
// payload agree still reads, and reads correctly.
TEST(ExternalPLYReader, StillAcceptsAWellFormedBinaryPLY)
{
    const std::string header =
        "ply\nformat binary_little_endian 1.0\n"
        "element vertex 1\n"
        "property float x\nproperty float y\nproperty float z\n"
        "end_header\n";

    std::vector<std::uint8_t> payload;
    for (const float v : { 1.0f, 2.0f, 3.0f }) {
        std::uint8_t b[4];
        std::memcpy(b, &v, 4);
        payload.insert(payload.end(), b, b + 4);
    }

    const auto result = wz::external::ply::read_ply_bytes(ply_bytes(header, payload));
    ASSERT_TRUE(result.ok) << result.error.message;

    const auto* table = find_table(result.document, "vertex");
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->row_count, 1u);
}
