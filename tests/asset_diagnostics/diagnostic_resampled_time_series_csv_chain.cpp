#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/type_extensions.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

namespace
{
    wz::engine::assets::DataTableData make_source_table()
    {
        wz::engine::assets::DataTableData table;
        table.columns.push_back({ .name = "time_ms" });
        table.columns.push_back({ .name = "frame_ms" });
        table.columns.push_back({ .name = "draw_calls" });

        table.rows.push_back({ .cells = { "0",   "1.0", "10" } });
        table.rows.push_back({ .cells = { "50",  "3.0", "10" } });
        table.rows.push_back({ .cells = { "120", "9.0", "12" } });
        table.rows.push_back({ .cells = { "180", "2.0", "14" } });
        table.rows.push_back({ .cells = { "240", "4.0", "16" } });

        return table;
    }
}

TEST(DiagnosticResampledTimeSeriesCSVChain, ResolvesFullChain)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_diagnostic_resampled_csv_chain_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const auto source_asset =
        assets.data_tables().create_inline_table({
            .name  = "diagnostics/source",
            .table = make_source_table(),
        });

    ASSERT_TRUE(source_asset.valid());

    const auto series_asset =
        assets.diagnostic_resampled_time_series().create_resampled_time_series({
            .name           = "diagnostics/source/resampled",
            .source         = source_asset,
            .axis_column    = "time_ms",
            .metric_columns = { "frame_ms", "draw_calls" },
            .bucket_width   = 100.0,
        });

    ASSERT_TRUE(series_asset.valid());

    const auto view_asset =
        assets.diagnostic_resampled_time_series().create_data_table_view(
            "diagnostics/source/resampled/table_view",
            series_asset);

    ASSERT_TRUE(view_asset.valid());

    const auto export_asset =
        assets.csv_export().create_csv_export({
            .name   = "diagnostics/source/resampled/csv",
            .source = view_asset,
        });

    ASSERT_TRUE(export_asset.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    // source table + resampled series + table view + csv export
    EXPECT_EQ(report.resolved_count, 4u);
}

TEST(DiagnosticResampledTimeSeriesCSVChain, TableViewHasCorrectColumnCount)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_diagnostic_resampled_csv_chain_column_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const auto source_asset =
        assets.data_tables().create_inline_table({
            .name  = "diagnostics/source",
            .table = make_source_table(),
        });

    const auto series_asset =
        assets.diagnostic_resampled_time_series().create_resampled_time_series({
            .name           = "diagnostics/source/resampled",
            .source         = source_asset,
            .axis_column    = "time_ms",
            .metric_columns = { "frame_ms" },
            .bucket_width   = 100.0,
        });

    const auto view_asset =
        assets.diagnostic_resampled_time_series().create_data_table_view(
            "diagnostics/source/resampled/table_view",
            series_asset);

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto view_handle = assets.data_tables().get_table(view_asset);
    ASSERT_TRUE(view_handle.valid());

    const auto* data = assets.data_tables().get_table_data(view_handle);
    ASSERT_NE(data, nullptr);

    // 4 base columns + 14 stat columns per metric = 18 for one metric
    EXPECT_EQ(data->column_count(), 18u);

    // Source has 5 rows spanning 0–240 ms with bucket_width=100 → 3 buckets
    EXPECT_EQ(data->row_count(), 3u);
}

TEST(DiagnosticResampledTimeSeriesCSVChain, CSVContainsExpectedHeaders)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_diagnostic_resampled_csv_chain_header_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const auto source_asset =
        assets.data_tables().create_inline_table({
            .name  = "diagnostics/source",
            .table = make_source_table(),
        });

    const auto series_asset =
        assets.diagnostic_resampled_time_series().create_resampled_time_series({
            .name           = "diagnostics/source/resampled",
            .source         = source_asset,
            .axis_column    = "time_ms",
            .metric_columns = { "frame_ms" },
            .bucket_width   = 100.0,
        });

    const auto view_asset =
        assets.diagnostic_resampled_time_series().create_data_table_view(
            "diagnostics/source/resampled/table_view",
            series_asset);

    const auto export_asset =
        assets.csv_export().create_csv_export({
            .name   = "diagnostics/source/resampled/csv",
            .source = view_asset,
        });

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto export_handle = assets.csv_export().get_export(export_asset);
    ASSERT_TRUE(export_handle.valid());

    const auto* csv_data = assets.csv_export().get_export_data(export_handle);
    ASSERT_NE(csv_data, nullptr);
    EXPECT_TRUE(csv_data->valid());

    const std::string& text = csv_data->csv_text;

    EXPECT_NE(text.find("bucket_index"),      std::string::npos);
    EXPECT_NE(text.find("bucket_start"),      std::string::npos);
    EXPECT_NE(text.find("bucket_end"),        std::string::npos);
    EXPECT_NE(text.find("source_row_count"),  std::string::npos);
    EXPECT_NE(text.find("frame_ms_min"),      std::string::npos);
    EXPECT_NE(text.find("frame_ms_max"),      std::string::npos);
    EXPECT_NE(text.find("frame_ms_mean"),     std::string::npos);

    EXPECT_EQ(csv_data->column_count, 18u);
    EXPECT_EQ(csv_data->row_count, 3u);
}

TEST(DiagnosticResampledTimeSeriesCSVChain, TableViewAssetTypeIsDataTable)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_diagnostic_resampled_csv_chain_type_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const auto source_asset =
        assets.data_tables().create_inline_table({
            .name  = "diagnostics/source",
            .table = make_source_table(),
        });

    const auto series_asset =
        assets.diagnostic_resampled_time_series().create_resampled_time_series({
            .name           = "diagnostics/source/resampled",
            .source         = source_asset,
            .axis_column    = "time_ms",
            .metric_columns = { "frame_ms" },
            .bucket_width   = 100.0,
        });

    const auto view_asset =
        assets.diagnostic_resampled_time_series().create_data_table_view(
            "diagnostics/source/resampled/table_view",
            series_asset);

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto view_handle = assets.data_tables().get_table(view_asset);
    ASSERT_TRUE(view_handle.valid());

    EXPECT_EQ(view_handle.handle.type, wz::engine::assets::kAssetTypeDataTable);
}
