#include <gtest/gtest.h>

#include <engine/assets/data_table/data_table.h>
#include <engine/assets/type_extensions.h>

TEST(DataTableData, RejectsEmptyColumns)
{
    wz::engine::assets::DataTableData table;

    EXPECT_FALSE(table.valid());
}

TEST(DataTableData, AcceptsHeaderOnlyTable)
{
    wz::engine::assets::DataTableData table;
    table.columns.push_back({ .name = "run_id" });
    table.columns.push_back({ .name = "frame_ms_avg" });

    EXPECT_TRUE(table.valid());
    EXPECT_EQ(table.column_count(), 2u);
    EXPECT_EQ(table.row_count(), 0u);
}

TEST(DataTableData, RejectsRowWithWrongCellCount)
{
    wz::engine::assets::DataTableData table;
    table.columns.push_back({ .name = "run_id" });
    table.columns.push_back({ .name = "frame_ms_avg" });

    table.rows.push_back({
        .cells = { "run_001" },
        });

    EXPECT_FALSE(table.valid());
}

TEST(DataTableData, AcceptsSimpleRows)
{
    wz::engine::assets::DataTableData table;
    table.columns.push_back({ .name = "run_id" });
    table.columns.push_back({ .name = "frame_ms_avg" });

    table.rows.push_back({
        .cells = { "run_001", "1.25" },
        });

    EXPECT_TRUE(table.valid());
    EXPECT_EQ(table.column_count(), 2u);
    EXPECT_EQ(table.row_count(), 1u);
}

TEST(DataTable, StoresAndRetrievesTable)
{
    wz::engine::assets::DataTable table_store;

    wz::engine::assets::DataTableData table;
    table.columns.push_back({ .name = "run_id" });
    table.columns.push_back({ .name = "frame_ms_avg" });
    table.rows.push_back({
        .cells = { "run_001", "1.25" },
        });

    const auto handle = table_store.add(std::move(table));

    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(handle.type, wz::engine::assets::kAssetTypeDataTable);

    const auto* stored = table_store.get(handle);

    ASSERT_NE(stored, nullptr);
    EXPECT_TRUE(stored->valid());
    EXPECT_EQ(stored->row_count(), 1u);
}

TEST(DataTable, RejectsWrongHandleType)
{
    wz::engine::assets::DataTable table_store;

    wz::engine::assets::DataTableData table;
    table.columns.push_back({ .name = "run_id" });

    const auto handle = table_store.add(std::move(table));
    ASSERT_TRUE(handle.valid());

    wz::asset::ResourceHandle wrong = handle;
    wrong.type = wz::asset::AssetType::Mesh;

    EXPECT_EQ(table_store.get(wrong), nullptr);
}
