#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/type_extensions.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

namespace
{
    // frame | frame_ms | draw_calls
    // Frames 0-9: 10 rows
    wz::engine::assets::DataTableData make_source_table()
    {
        wz::engine::assets::DataTableData table;
        table.columns.push_back({ .name = "frame" });
        table.columns.push_back({ .name = "frame_ms" });
        table.columns.push_back({ .name = "draw_calls" });

        for (uint32_t f = 0; f < 20; ++f) {
            const double ms   = 1.0 + static_cast<double>(f) * 0.5;
            const double calls = 10.0 + static_cast<double>(f);
            table.rows.push_back({ .cells = {
                std::to_string(f),
                std::to_string(ms),
                std::to_string(calls),
            }});
        }

        return table;
    }

    wz::engine::assets::EngineAssetLibrary make_library(const wz::fs::Path& root)
    {
        wz::Logger logger;
        wz::gpu::Device device{};
        return wz::engine::assets::EngineAssetLibrary{ device, logger, root };
    }
}

// ─── Frame filtering ──────────────────────────────────────────────────────────

TEST(DiagnosticTimeframeSummaryAssetModule, FiltersRowsByFrameColumn)
{
    const auto root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_tfs_filter_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const auto source = assets.data_tables().create_inline_table({
        .name = "diag/source", .table = make_source_table() });

    // Only frames 5-9 (5 rows)
    const auto summary_asset =
        assets.diagnostic_timeframe_summaries().create_timeframe_summary({
            .name           = "diag/summary",
            .source         = source,
            .frame_column   = "frame",
            .metric_columns = { "frame_ms" },
            .frame_start    = 5,
            .frame_end      = 9,
        });

    ASSERT_TRUE(summary_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto handle = assets.diagnostic_timeframe_summaries().get_summary(summary_asset);
    ASSERT_TRUE(handle.valid());

    const auto* data = assets.diagnostic_timeframe_summaries().get_summary_data(handle);
    ASSERT_NE(data, nullptr);
    ASSERT_TRUE(data->valid());

    ASSERT_EQ(data->bucket_count(), 1u);
    EXPECT_EQ(data->buckets[0].row_count, 5u);
}

// ─── min / max / mean / delta ─────────────────────────────────────────────────

TEST(DiagnosticTimeframeSummaryAssetModule, ComputesCorrectStats)
{
    const auto root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_tfs_stats_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    // Simple table: frame 0→frame_ms=1.0, frame 1→2.0, frame 2→3.0
    wz::engine::assets::DataTableData table;
    table.columns.push_back({ .name = "frame" });
    table.columns.push_back({ .name = "frame_ms" });
    table.rows.push_back({ .cells = { "0", "1.0" } });
    table.rows.push_back({ .cells = { "1", "2.0" } });
    table.rows.push_back({ .cells = { "2", "3.0" } });

    const auto source = assets.data_tables().create_inline_table({
        .name = "diag/source", .table = std::move(table) });

    const auto summary_asset =
        assets.diagnostic_timeframe_summaries().create_timeframe_summary({
            .name           = "diag/summary",
            .source         = source,
            .frame_column   = "frame",
            .metric_columns = { "frame_ms" },
            .frame_start    = 0,
            .frame_end      = 2,
        });

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* data =
        assets.diagnostic_timeframe_summaries().get_summary_data(
            assets.diagnostic_timeframe_summaries().get_summary(summary_asset));

    ASSERT_NE(data, nullptr);
    ASSERT_EQ(data->bucket_count(), 1u);
    ASSERT_EQ(data->buckets[0].metrics.size(), 1u);

    const auto& m = data->buckets[0].metrics[0];

    EXPECT_EQ(m.sample_count, 3u);
    EXPECT_DOUBLE_EQ(m.min,   1.0);
    EXPECT_DOUBLE_EQ(m.max,   3.0);
    EXPECT_DOUBLE_EQ(m.mean,  2.0);
    EXPECT_DOUBLE_EQ(m.first, 1.0);
    EXPECT_DOUBLE_EQ(m.last,  3.0);
    EXPECT_DOUBLE_EQ(m.delta, 2.0);   // last - first
}

// ─── N frames per bucket ──────────────────────────────────────────────────────

TEST(DiagnosticTimeframeSummaryAssetModule, FramesPerBucketProducesMultipleBuckets)
{
    const auto root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_tfs_buckets_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const auto source = assets.data_tables().create_inline_table({
        .name = "diag/source", .table = make_source_table() });

    // frames 0-19, 5 frames per bucket → 4 buckets
    const auto summary_asset =
        assets.diagnostic_timeframe_summaries().create_timeframe_summary({
            .name              = "diag/summary",
            .source            = source,
            .frame_column      = "frame",
            .metric_columns    = { "frame_ms" },
            .frame_start       = 0,
            .frame_end         = 19,
            .frames_per_bucket = 5,
        });

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* data =
        assets.diagnostic_timeframe_summaries().get_summary_data(
            assets.diagnostic_timeframe_summaries().get_summary(summary_asset));

    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->bucket_count(), 4u);

    EXPECT_EQ(data->buckets[0].frame_start, 0u);
    EXPECT_EQ(data->buckets[0].frame_end,   4u);
    EXPECT_EQ(data->buckets[0].row_count,   5u);

    EXPECT_EQ(data->buckets[1].frame_start, 5u);
    EXPECT_EQ(data->buckets[1].frame_end,   9u);
    EXPECT_EQ(data->buckets[1].row_count,   5u);

    EXPECT_EQ(data->buckets[3].frame_start, 15u);
    EXPECT_EQ(data->buckets[3].frame_end,   19u);
    EXPECT_EQ(data->buckets[3].row_count,   5u);
}

TEST(DiagnosticTimeframeSummaryAssetModule, FramesPerBucketHandlesUnevenRange)
{
    const auto root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_tfs_uneven_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const auto source = assets.data_tables().create_inline_table({
        .name = "diag/source", .table = make_source_table() });

    // frames 0-11 with 5 per bucket: [0-4],[5-9],[10-11] → 3 buckets
    const auto summary_asset =
        assets.diagnostic_timeframe_summaries().create_timeframe_summary({
            .name              = "diag/summary",
            .source            = source,
            .frame_column      = "frame",
            .metric_columns    = { "frame_ms" },
            .frame_start       = 0,
            .frame_end         = 11,
            .frames_per_bucket = 5,
        });

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* data =
        assets.diagnostic_timeframe_summaries().get_summary_data(
            assets.diagnostic_timeframe_summaries().get_summary(summary_asset));

    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->bucket_count(), 3u);

    EXPECT_EQ(data->buckets[2].frame_start, 10u);
    EXPECT_EQ(data->buckets[2].frame_end,   11u);
    EXPECT_EQ(data->buckets[2].row_count,   2u);
}

// ─── Invalid column names ─────────────────────────────────────────────────────

TEST(DiagnosticTimeframeSummaryAssetModule, RejectsMissingFrameColumn)
{
    const auto root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_tfs_bad_frame_col_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const auto source = assets.data_tables().create_inline_table({
        .name = "diag/source", .table = make_source_table() });

    const auto summary_asset =
        assets.diagnostic_timeframe_summaries().create_timeframe_summary({
            .name           = "diag/summary",
            .source         = source,
            .frame_column   = "nonexistent_frame_col",
            .metric_columns = { "frame_ms" },
            .frame_start    = 0,
            .frame_end      = 9,
        });

    ASSERT_TRUE(summary_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_FALSE(report.ok());
}

TEST(DiagnosticTimeframeSummaryAssetModule, RejectsMissingMetricColumn)
{
    const auto root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_tfs_bad_metric_col_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const auto source = assets.data_tables().create_inline_table({
        .name = "diag/source", .table = make_source_table() });

    const auto summary_asset =
        assets.diagnostic_timeframe_summaries().create_timeframe_summary({
            .name           = "diag/summary",
            .source         = source,
            .frame_column   = "frame",
            .metric_columns = { "nonexistent_metric" },
            .frame_start    = 0,
            .frame_end      = 9,
        });

    ASSERT_TRUE(summary_asset.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    EXPECT_FALSE(report.ok());
}

// ─── Empty frame range ────────────────────────────────────────────────────────

TEST(DiagnosticTimeframeSummaryAssetModule, EmptyRangeProducesZeroRowBucket)
{
    const auto root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_tfs_empty_range_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const auto source = assets.data_tables().create_inline_table({
        .name = "diag/source", .table = make_source_table() });

    // Frame range 100-200 — no rows in source match
    const auto summary_asset =
        assets.diagnostic_timeframe_summaries().create_timeframe_summary({
            .name           = "diag/summary",
            .source         = source,
            .frame_column   = "frame",
            .metric_columns = { "frame_ms" },
            .frame_start    = 100,
            .frame_end      = 200,
        });

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* data =
        assets.diagnostic_timeframe_summaries().get_summary_data(
            assets.diagnostic_timeframe_summaries().get_summary(summary_asset));

    ASSERT_NE(data, nullptr);
    ASSERT_EQ(data->bucket_count(), 1u);
    EXPECT_EQ(data->buckets[0].row_count, 0u);
    EXPECT_EQ(data->buckets[0].metrics[0].sample_count, 0u);
}

// ─── summary_text ─────────────────────────────────────────────────────────────

TEST(DiagnosticTimeframeSummaryAssetModule, SummaryTextContainsExpectedContent)
{
    const auto root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_tfs_summary_text_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const auto source = assets.data_tables().create_inline_table({
        .name = "diag/source", .table = make_source_table() });

    const auto summary_asset =
        assets.diagnostic_timeframe_summaries().create_timeframe_summary({
            .name           = "diag/summary",
            .source         = source,
            .frame_column   = "frame",
            .metric_columns = { "frame_ms" },
            .frame_start    = 0,
            .frame_end      = 4,
        });

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* data =
        assets.diagnostic_timeframe_summaries().get_summary_data(
            assets.diagnostic_timeframe_summaries().get_summary(summary_asset));

    ASSERT_NE(data, nullptr);
    ASSERT_EQ(data->bucket_count(), 1u);

    const std::string& text = data->buckets[0].summary_text;

    EXPECT_NE(text.find("frames 0-4"),  std::string::npos);
    EXPECT_NE(text.find("rows=5"),      std::string::npos);
    EXPECT_NE(text.find("frame_ms:"),   std::string::npos);
    EXPECT_NE(text.find("min="),        std::string::npos);
    EXPECT_NE(text.find("max="),        std::string::npos);
    EXPECT_NE(text.find("mean="),       std::string::npos);
    EXPECT_NE(text.find("delta="),      std::string::npos);
}

TEST(DiagnosticTimeframeSummaryAssetModule, SummaryTextIsDeterministic)
{
    const auto root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_tfs_deterministic_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const auto source = assets.data_tables().create_inline_table({
        .name = "diag/source", .table = make_source_table() });

    const auto a = assets.diagnostic_timeframe_summaries().create_timeframe_summary({
        .name = "diag/a", .source = source,
        .frame_column = "frame", .metric_columns = { "frame_ms" },
        .frame_start = 0, .frame_end = 4,
    });

    const auto b = assets.diagnostic_timeframe_summaries().create_timeframe_summary({
        .name = "diag/b", .source = source,
        .frame_column = "frame", .metric_columns = { "frame_ms" },
        .frame_start = 0, .frame_end = 4,
    });

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* da = assets.diagnostic_timeframe_summaries().get_summary_data(
        assets.diagnostic_timeframe_summaries().get_summary(a));
    const auto* db = assets.diagnostic_timeframe_summaries().get_summary_data(
        assets.diagnostic_timeframe_summaries().get_summary(b));

    ASSERT_NE(da, nullptr);
    ASSERT_NE(db, nullptr);

    EXPECT_EQ(da->buckets[0].summary_text, db->buckets[0].summary_text);
}
