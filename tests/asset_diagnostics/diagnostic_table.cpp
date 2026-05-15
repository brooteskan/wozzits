#include <gtest/gtest.h>

#include <engine/assets/diagnostics/diagnostic_table.h>
#include <engine/assets/type_extensions.h>

TEST(DiagnosticTableData, RejectsEmptyColumns)
{
    wz::engine::assets::DiagnosticTableData table;

    EXPECT_FALSE(table.valid());
}

TEST(DiagnosticTableData, AcceptsHeaderOnlyTable)
{
    wz::engine::assets::DiagnosticTableData table;
    table.columns.push_back({ .name = "run_id" });
    table.columns.push_back({ .name = "frame_ms_avg" });

    EXPECT_TRUE(table.valid());
    EXPECT_EQ(table.column_count(), 2u);
    EXPECT_EQ(table.row_count(), 0u);
}

TEST(DiagnosticTableData, RejectsRowWithWrongCellCount)
{
    wz::engine::assets::DiagnosticTableData table;
    table.columns.push_back({ .name = "run_id" });
    table.columns.push_back({ .name = "frame_ms_avg" });

    table.rows.push_back({
        .cells = { "run_001" },
        });

    EXPECT_FALSE(table.valid());
}

TEST(DiagnosticTableData, AcceptsSimpleRows)
{
    wz::engine::assets::DiagnosticTableData table;
    table.columns.push_back({ .name = "run_id" });
    table.columns.push_back({ .name = "frame_ms_avg" });

    table.rows.push_back({
        .cells = { "run_001", "1.25" },
        });

    EXPECT_TRUE(table.valid());
    EXPECT_EQ(table.column_count(), 2u);
    EXPECT_EQ(table.row_count(), 1u);
}

TEST(DiagnosticTable, StoresAndRetrievesTable)
{
    wz::engine::assets::DiagnosticTable table_store;

    wz::engine::assets::DiagnosticTableData table;
    table.columns.push_back({ .name = "run_id" });
    table.columns.push_back({ .name = "frame_ms_avg" });
    table.rows.push_back({
        .cells = { "run_001", "1.25" },
        });

    const auto handle = table_store.add(std::move(table));

    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(handle.type, wz::engine::assets::kAssetTypeDiagnosticTable);

    const auto* stored = table_store.get(handle);

    ASSERT_NE(stored, nullptr);
    EXPECT_TRUE(stored->valid());
    EXPECT_EQ(stored->row_count(), 1u);
}

TEST(DiagnosticTable, RejectsWrongHandleType)
{
    wz::engine::assets::DiagnosticTable table_store;

    wz::engine::assets::DiagnosticTableData table;
    table.columns.push_back({ .name = "run_id" });

    const auto handle = table_store.add(std::move(table));
    ASSERT_TRUE(handle.valid());

    wz::asset::ResourceHandle wrong = handle;
    wrong.type = wz::asset::AssetType::Mesh;

    EXPECT_EQ(table_store.get(wrong), nullptr);
}