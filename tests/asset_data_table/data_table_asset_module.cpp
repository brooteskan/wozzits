#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/type_extensions.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

TEST(DataTableAssetModule, ResolvesInlineTable)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_data_table_asset_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    wz::engine::assets::DataTableData table;
    table.columns.push_back({ .name = "run_id" });
    table.columns.push_back({ .name = "scene_mode" });
    table.columns.push_back({ .name = "frame_ms_avg" });

    table.rows.push_back({
        .cells = { "run_001", "max_triangles", "1.25" },
        });

    const auto asset =
        assets.data_tables().create_inline_table({
            .name = "benchmark/run_001",
            .table = table,
            });

    ASSERT_TRUE(asset.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 1u);

    const auto handle =
        assets.data_tables().get_table(asset);

    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(
        handle.handle.type,
        wz::engine::assets::kAssetTypeDataTable);

    const auto* stored =
        assets.data_tables().get_table_data(handle);

    ASSERT_NE(stored, nullptr);
    EXPECT_TRUE(stored->valid());
    EXPECT_EQ(stored->column_count(), 3u);
    EXPECT_EQ(stored->row_count(), 1u);
    EXPECT_EQ(stored->rows[0].cells[1], "max_triangles");
}

TEST(DataTableAssetModule, RejectsInvalidInlineTable)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_data_table_invalid_asset_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    wz::engine::assets::DataTableData table;
    table.rows.push_back({
        .cells = { "row_without_columns" },
        });

    const auto asset =
        assets.data_tables().create_inline_table({
            .name = "benchmark/invalid",
            .table = table,
            });

    EXPECT_FALSE(asset.valid());
}
