#include <gtest/gtest.h>

#include <engine/assets/csv_export/csv_export.h>
#include <engine/assets/type_extensions.h>

TEST(CSVExportData, RejectsZeroColumns)
{
    wz::engine::assets::CSVExportData data;
    EXPECT_FALSE(data.valid());
}

TEST(CSVExportData, AcceptsNonZeroColumnCount)
{
    wz::engine::assets::CSVExportData data;
    data.csv_text     = "name,value\r\n";
    data.column_count = 2;
    data.row_count    = 0;

    EXPECT_TRUE(data.valid());
}

TEST(CSVExportData, AcceptsZeroRowsWithColumns)
{
    wz::engine::assets::CSVExportData data;
    data.csv_text     = "name\r\n";
    data.column_count = 1;
    data.row_count    = 0;

    EXPECT_TRUE(data.valid());
}

TEST(CSVExportTable, StoresAndRetrievesExport)
{
    wz::engine::assets::CSVExportTable table;

    wz::engine::assets::CSVExportData data;
    data.csv_text     = "a,b\r\nalpha,1\r\n";
    data.column_count = 2;
    data.row_count    = 1;

    const auto handle = table.add(std::move(data));

    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(handle.type, wz::engine::assets::kAssetTypeCSVExport);

    const auto* stored = table.get(handle);

    ASSERT_NE(stored, nullptr);
    EXPECT_TRUE(stored->valid());
    EXPECT_EQ(stored->csv_text, "a,b\r\nalpha,1\r\n");
    EXPECT_EQ(stored->column_count, 2u);
    EXPECT_EQ(stored->row_count, 1u);
}

TEST(CSVExportTable, RejectsInvalidData)
{
    wz::engine::assets::CSVExportTable table;

    wz::engine::assets::CSVExportData data;  // column_count == 0

    const auto handle = table.add(std::move(data));
    EXPECT_FALSE(handle.valid());
}

TEST(CSVExportTable, RejectsWrongHandleType)
{
    wz::engine::assets::CSVExportTable table;

    wz::engine::assets::CSVExportData data;
    data.csv_text     = "x\r\n";
    data.column_count = 1;

    const auto handle = table.add(std::move(data));
    ASSERT_TRUE(handle.valid());

    wz::asset::ResourceHandle wrong = handle;
    wrong.type = wz::asset::AssetType::Mesh;

    EXPECT_EQ(table.get(wrong), nullptr);
}
