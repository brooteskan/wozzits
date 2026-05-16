#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/type_extensions.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

namespace
{
    // frame | frame_ms | draw_calls, frames 0-9
    wz::engine::assets::DataTableData make_source_table()
    {
        wz::engine::assets::DataTableData table;
        table.columns.push_back({ .name = "frame"      });
        table.columns.push_back({ .name = "frame_ms"   });
        table.columns.push_back({ .name = "draw_calls" });

        for (uint32_t f = 0; f < 10; ++f) {
            table.rows.push_back({ .cells = {
                std::to_string(f),
                std::to_string(1.0 + static_cast<double>(f) * 0.5),
                std::to_string(10 + f),
            }});
        }

        return table;
    }
}

TEST(DiagnosticTimeframeSummaryCSVChain, ResolvesFullChain)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_tfs_csv_chain_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const auto source_asset =
        assets.data_tables().create_inline_table({
            .name  = "diag/source",
            .table = make_source_table(),
        });

    ASSERT_TRUE(source_asset.valid());

    const auto summary_asset =
        assets.diagnostic_timeframe_summaries().create_timeframe_summary({
            .name           = "diag/summary",
            .source         = source_asset,
            .frame_column   = "frame",
            .metric_columns = { "frame_ms" },
            .frame_start    = 0,
            .frame_end      = 9,
        });

    ASSERT_TRUE(summary_asset.valid());

    const auto view_asset =
        assets.diagnostic_timeframe_summaries().create_data_table_view(
            "diag/summary/table_view",
            summary_asset);

    ASSERT_TRUE(view_asset.valid());

    const auto export_asset =
        assets.csv_export().create_csv_export({
            .name   = "diag/summary/csv",
            .source = view_asset,
        });

    ASSERT_TRUE(export_asset.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    // source table + summary + table view + csv export
    EXPECT_EQ(report.resolved_count, 4u);
}

TEST(DiagnosticTimeframeSummaryCSVChain, TableViewHasCorrectColumnCount)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_tfs_csv_chain_col_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const auto source_asset =
        assets.data_tables().create_inline_table({
            .name  = "diag/source",
            .table = make_source_table(),
        });

    const auto summary_asset =
        assets.diagnostic_timeframe_summaries().create_timeframe_summary({
            .name              = "diag/summary",
            .source            = source_asset,
            .frame_column      = "frame",
            .metric_columns    = { "frame_ms" },
            .frame_start       = 0,
            .frame_end         = 9,
            .frames_per_bucket = 5,
        });

    const auto view_asset =
        assets.diagnostic_timeframe_summaries().create_data_table_view(
            "diag/summary/table_view",
            summary_asset);

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto view_handle = assets.data_tables().get_table(view_asset);
    ASSERT_TRUE(view_handle.valid());

    const auto* data = assets.data_tables().get_table_data(view_handle);
    ASSERT_NE(data, nullptr);

    // 4 base cols (bucket_index, frame_start, frame_end, row_count)
    // + 7 per metric (sample_count, min, max, mean, first, last, delta)
    // = 11 for 1 metric
    EXPECT_EQ(data->column_count(), 11u);

    // frames 0-9 with 5/bucket → 2 buckets → 2 rows
    EXPECT_EQ(data->row_count(), 2u);
}

TEST(DiagnosticTimeframeSummaryCSVChain, CSVContainsExpectedHeaders)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_tfs_csv_chain_header_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const auto source_asset =
        assets.data_tables().create_inline_table({
            .name  = "diag/source",
            .table = make_source_table(),
        });

    const auto summary_asset =
        assets.diagnostic_timeframe_summaries().create_timeframe_summary({
            .name           = "diag/summary",
            .source         = source_asset,
            .frame_column   = "frame",
            .metric_columns = { "frame_ms" },
            .frame_start    = 0,
            .frame_end      = 9,
        });

    const auto view_asset =
        assets.diagnostic_timeframe_summaries().create_data_table_view(
            "diag/summary/table_view",
            summary_asset);

    const auto export_asset =
        assets.csv_export().create_csv_export({
            .name   = "diag/summary/csv",
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
    EXPECT_NE(text.find("frame_start"),       std::string::npos);
    EXPECT_NE(text.find("frame_end"),         std::string::npos);
    EXPECT_NE(text.find("row_count"),         std::string::npos);
    EXPECT_NE(text.find("frame_ms_min"),      std::string::npos);
    EXPECT_NE(text.find("frame_ms_max"),      std::string::npos);
    EXPECT_NE(text.find("frame_ms_mean"),     std::string::npos);
    EXPECT_NE(text.find("frame_ms_delta"),    std::string::npos);

    EXPECT_EQ(csv_data->column_count, 11u);
    EXPECT_EQ(csv_data->row_count, 1u);  // frames 0-9 as one bucket
}

TEST(DiagnosticTimeframeSummaryCSVChain, TableViewAssetTypeIsDataTable)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_tfs_csv_chain_type_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const auto source_asset =
        assets.data_tables().create_inline_table({
            .name  = "diag/source",
            .table = make_source_table(),
        });

    const auto summary_asset =
        assets.diagnostic_timeframe_summaries().create_timeframe_summary({
            .name           = "diag/summary",
            .source         = source_asset,
            .frame_column   = "frame",
            .metric_columns = { "frame_ms" },
            .frame_start    = 0,
            .frame_end      = 9,
        });

    const auto view_asset =
        assets.diagnostic_timeframe_summaries().create_data_table_view(
            "diag/summary/table_view",
            summary_asset);

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto view_handle = assets.data_tables().get_table(view_asset);
    ASSERT_TRUE(view_handle.valid());

    EXPECT_EQ(view_handle.handle.type, wz::engine::assets::kAssetTypeDataTable);
}
