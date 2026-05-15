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

        table.rows.push_back({ .cells = { "0",   "1.0",  "10" } });
        table.rows.push_back({ .cells = { "50",  "3.0",  "10" } });
        table.rows.push_back({ .cells = { "120", "9.0",  "12" } });
        table.rows.push_back({ .cells = { "180", "2.0",  "14" } });
        table.rows.push_back({ .cells = { "240", "4.0",  "16" } });

        return table;
    }
}

TEST(DiagnosticResampledTimeSeriesAssetModule, ResolvesResampledSeries)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_diagnostic_resampled_time_series_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    const auto table_asset =
        assets.data_tables().create_inline_table({
            .name = "diagnostics/source",
            .table = make_source_table(),
            });

    ASSERT_TRUE(table_asset.valid());

    const auto resampled_asset =
        assets.diagnostic_resampled_time_series().create_resampled_time_series({
            .name = "diagnostics/source/resampled_100ms",
            .source = table_asset,
            .axis_column = "time_ms",
            .metric_columns = { "frame_ms", "draw_calls" },
            .bucket_width = 100.0,
            });

    ASSERT_TRUE(resampled_asset.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto handle =
        assets.diagnostic_resampled_time_series()
        .get_resampled_time_series(resampled_asset);

    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(
        handle.handle.type,
        wz::engine::assets::kAssetTypeDiagnosticResampledTimeSeries);

    const auto* data =
        assets.diagnostic_resampled_time_series()
        .get_resampled_time_series_data(handle);

    ASSERT_NE(data, nullptr);
    ASSERT_TRUE(data->valid());

    EXPECT_EQ(data->axis_column, "time_ms");
    EXPECT_EQ(data->metric_columns.size(), 2u);

    // Source range is 0..240. With 100ms buckets, expect:
    // [0,100), [100,200), [200,240]
    ASSERT_EQ(data->bucket_count(), 3u);

    const auto& b0 = data->buckets[0];
    ASSERT_EQ(b0.metrics.size(), 2u);

    const auto& frame_ms_0 = b0.metrics[0];

    EXPECT_EQ(frame_ms_0.sample_count, 2u);
    EXPECT_DOUBLE_EQ(frame_ms_0.min, 1.0);
    EXPECT_DOUBLE_EQ(frame_ms_0.max, 3.0);
    EXPECT_DOUBLE_EQ(frame_ms_0.mean, 2.0);
    EXPECT_DOUBLE_EQ(frame_ms_0.first, 1.0);
    EXPECT_DOUBLE_EQ(frame_ms_0.last, 3.0);

    EXPECT_DOUBLE_EQ(frame_ms_0.min_axis, 0.0);
    EXPECT_DOUBLE_EQ(frame_ms_0.max_axis, 50.0);
    EXPECT_EQ(frame_ms_0.min_source_row, 0u);
    EXPECT_EQ(frame_ms_0.max_source_row, 1u);

    const auto& b1 = data->buckets[1];
    const auto& frame_ms_1 = b1.metrics[0];

    EXPECT_EQ(frame_ms_1.sample_count, 2u);
    EXPECT_DOUBLE_EQ(frame_ms_1.min, 2.0);
    EXPECT_DOUBLE_EQ(frame_ms_1.max, 9.0);
    EXPECT_DOUBLE_EQ(frame_ms_1.mean, 5.5);

    // This verifies the transient is preserved even though the bucket mean is 5.5.
    EXPECT_DOUBLE_EQ(frame_ms_1.max_axis, 120.0);
    EXPECT_EQ(frame_ms_1.max_source_row, 2u);
}

TEST(DiagnosticResampledTimeSeriesAssetModule, SupportsExplicitRange)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_diagnostic_resampled_time_series_range_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    const auto table_asset =
        assets.data_tables().create_inline_table({
            .name = "diagnostics/source",
            .table = make_source_table(),
            });

    ASSERT_TRUE(table_asset.valid());

    const auto resampled_asset =
        assets.diagnostic_resampled_time_series().create_resampled_time_series({
            .name = "diagnostics/source/resampled_range",
            .source = table_asset,
            .axis_column = "time_ms",
            .metric_columns = { "frame_ms" },
            .bucket_width = 50.0,
            .use_range = true,
            .start = 100.0,
            .end = 200.0,
            });

    ASSERT_TRUE(resampled_asset.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());

    const auto handle =
        assets.diagnostic_resampled_time_series()
        .get_resampled_time_series(resampled_asset);

    ASSERT_TRUE(handle.valid());

    const auto* data =
        assets.diagnostic_resampled_time_series()
        .get_resampled_time_series_data(handle);

    ASSERT_NE(data, nullptr);
    ASSERT_TRUE(data->valid());

    EXPECT_DOUBLE_EQ(data->source_start, 100.0);
    EXPECT_DOUBLE_EQ(data->source_end, 200.0);

    ASSERT_EQ(data->bucket_count(), 2u);

    // Rows at 120 and 180 are selected.
    EXPECT_EQ(data->parsed_row_count, 2u);
}

TEST(DiagnosticResampledTimeSeriesAssetModule, RejectsMissingMetricColumn)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_diagnostic_resampled_time_series_missing_column_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    const auto table_asset =
        assets.data_tables().create_inline_table({
            .name = "diagnostics/source",
            .table = make_source_table(),
            });

    ASSERT_TRUE(table_asset.valid());

    const auto resampled_asset =
        assets.diagnostic_resampled_time_series().create_resampled_time_series({
            .name = "diagnostics/source/resampled_missing",
            .source = table_asset,
            .axis_column = "time_ms",
            .metric_columns = { "missing_metric" },
            .bucket_width = 100.0,
            });

    ASSERT_TRUE(resampled_asset.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_FALSE(report.ok());
}