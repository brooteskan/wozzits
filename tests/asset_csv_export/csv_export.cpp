#include <gtest/gtest.h>

#include <engine/assets/csv_export/csv_export.h>
#include <engine/assets/type_extensions.h>

TEST(CSVExportData, RejectsEmptyOutputPath)
{
    wz::engine::assets::CSVExportData data;
    EXPECT_FALSE(data.valid());
}

TEST(CSVExportData, AcceptsDataWithPath)
{
    wz::engine::assets::CSVExportData data;
    data.output_path  = "out/export.csv";
    data.column_count = 2;
    data.row_count    = 3;

    EXPECT_TRUE(data.valid());
}

TEST(CSVExportTable, StoresAndRetrievesExport)
{
    wz::engine::assets::CSVExportTable table;

    wz::engine::assets::CSVExportData data;
    data.output_path  = "out/export.csv";
    data.column_count = 3;
    data.row_count    = 5;

    const auto handle = table.add(std::move(data));

    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(handle.type, wz::engine::assets::kAssetTypeCSVExport);

    const auto* stored = table.get(handle);

    ASSERT_NE(stored, nullptr);
    EXPECT_TRUE(stored->valid());
    EXPECT_EQ(stored->output_path, "out/export.csv");
    EXPECT_EQ(stored->column_count, 3u);
    EXPECT_EQ(stored->row_count, 5u);
}

TEST(CSVExportTable, RejectsInvalidData)
{
    wz::engine::assets::CSVExportTable table;

    wz::engine::assets::CSVExportData data;  // empty output_path

    const auto handle = table.add(std::move(data));
    EXPECT_FALSE(handle.valid());
}

TEST(CSVExportTable, RejectsWrongHandleType)
{
    wz::engine::assets::CSVExportTable table;

    wz::engine::assets::CSVExportData data;
    data.output_path = "out/export.csv";

    const auto handle = table.add(std::move(data));
    ASSERT_TRUE(handle.valid());

    wz::asset::ResourceHandle wrong = handle;
    wrong.type = wz::asset::AssetType::Mesh;

    EXPECT_EQ(table.get(wrong), nullptr);
}
