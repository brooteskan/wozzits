#include <gtest/gtest.h>

#include <engine/assets/diagnostics/diagnostic_resampled_time_series.h>
#include <engine/assets/data_table/data_table.h>

using namespace wz::engine::assets;

namespace
{
    DiagnosticResampledTimeSeriesData make_series(
        std::vector<std::string> metric_columns,
        uint32_t bucket_count = 2)
    {
        DiagnosticResampledTimeSeriesData series;
        series.axis_column    = "time_ms";
        series.metric_columns = std::move(metric_columns);
        series.bucket_width   = 100.0;
        series.source_start   = 0.0;
        series.source_end     = static_cast<double>(bucket_count) * 100.0;

        for (uint32_t i = 0; i < bucket_count; ++i) {
            DiagnosticResampledBucket bucket;
            bucket.start            = static_cast<double>(i) * 100.0;
            bucket.end              = static_cast<double>(i + 1) * 100.0;
            bucket.source_row_count = i + 1;

            for (size_t m = 0; m < series.metric_columns.size(); ++m) {
                DiagnosticMetricBucketStats stats;
                stats.sample_count    = i + 1;
                stats.min             = static_cast<double>(i) * 1.0;
                stats.max             = static_cast<double>(i) * 2.0;
                stats.mean            = static_cast<double>(i) * 1.5;
                stats.first           = static_cast<double>(i) * 1.1;
                stats.last            = static_cast<double>(i) * 1.9;
                stats.min_axis        = static_cast<double>(i) * 10.0;
                stats.max_axis        = static_cast<double>(i) * 20.0;
                stats.first_axis      = static_cast<double>(i) * 11.0;
                stats.last_axis       = static_cast<double>(i) * 19.0;
                stats.min_source_row  = i * 10;
                stats.max_source_row  = i * 10 + 1;
                stats.first_source_row = i * 10 + 2;
                stats.last_source_row  = i * 10 + 3;
                bucket.metrics.push_back(stats);
            }

            series.buckets.push_back(std::move(bucket));
        }

        return series;
    }
}

TEST(DiagnosticResampledTimeSeriesTableView, ColumnLayout_OneMetric)
{
    const auto series  = make_series({ "frame_ms" });
    const auto table   = make_data_table(series);

    // 4 base + 14 per metric = 18
    ASSERT_EQ(table.column_count(), 18u);

    EXPECT_EQ(table.columns[0].name,  "bucket_index");
    EXPECT_EQ(table.columns[1].name,  "bucket_start");
    EXPECT_EQ(table.columns[2].name,  "bucket_end");
    EXPECT_EQ(table.columns[3].name,  "source_row_count");

    EXPECT_EQ(table.columns[4].name,  "frame_ms_sample_count");
    EXPECT_EQ(table.columns[5].name,  "frame_ms_min");
    EXPECT_EQ(table.columns[6].name,  "frame_ms_max");
    EXPECT_EQ(table.columns[7].name,  "frame_ms_mean");
    EXPECT_EQ(table.columns[8].name,  "frame_ms_first");
    EXPECT_EQ(table.columns[9].name,  "frame_ms_last");
    EXPECT_EQ(table.columns[10].name, "frame_ms_min_axis");
    EXPECT_EQ(table.columns[11].name, "frame_ms_max_axis");
    EXPECT_EQ(table.columns[12].name, "frame_ms_first_axis");
    EXPECT_EQ(table.columns[13].name, "frame_ms_last_axis");
    EXPECT_EQ(table.columns[14].name, "frame_ms_min_source_row");
    EXPECT_EQ(table.columns[15].name, "frame_ms_max_source_row");
    EXPECT_EQ(table.columns[16].name, "frame_ms_first_source_row");
    EXPECT_EQ(table.columns[17].name, "frame_ms_last_source_row");
}

TEST(DiagnosticResampledTimeSeriesTableView, ColumnLayout_TwoMetrics)
{
    const auto series = make_series({ "frame_ms", "cpu_ms" });
    const auto table  = make_data_table(series);

    // 4 base + 14 * 2 = 32
    ASSERT_EQ(table.column_count(), 32u);

    EXPECT_EQ(table.columns[4].name,  "frame_ms_sample_count");
    EXPECT_EQ(table.columns[17].name, "frame_ms_last_source_row");
    EXPECT_EQ(table.columns[18].name, "cpu_ms_sample_count");
    EXPECT_EQ(table.columns[31].name, "cpu_ms_last_source_row");
}

TEST(DiagnosticResampledTimeSeriesTableView, RowCountMatchesBucketCount)
{
    const auto series = make_series({ "frame_ms" }, 5);
    const auto table  = make_data_table(series);

    EXPECT_EQ(table.row_count(), 5u);
}

TEST(DiagnosticResampledTimeSeriesTableView, RowCellCountMatchesColumnCount)
{
    const auto series = make_series({ "frame_ms", "cpu_ms" }, 3);
    const auto table  = make_data_table(series);

    ASSERT_TRUE(table.valid());

    for (const DataTableRow& row : table.rows) {
        EXPECT_EQ(row.cells.size(), table.column_count());
    }
}

TEST(DiagnosticResampledTimeSeriesTableView, BucketIndexCells)
{
    const auto series = make_series({ "frame_ms" }, 3);
    const auto table  = make_data_table(series);

    ASSERT_EQ(table.row_count(), 3u);

    EXPECT_EQ(table.rows[0].cells[0], "0");
    EXPECT_EQ(table.rows[1].cells[0], "1");
    EXPECT_EQ(table.rows[2].cells[0], "2");
}

TEST(DiagnosticResampledTimeSeriesTableView, SourceRowCountCell)
{
    const auto series = make_series({ "frame_ms" }, 2);
    const auto table  = make_data_table(series);

    // make_series sets source_row_count = i + 1
    EXPECT_EQ(table.rows[0].cells[3], "1");
    EXPECT_EQ(table.rows[1].cells[3], "2");
}

TEST(DiagnosticResampledTimeSeriesTableView, MetricSampleCountCell)
{
    const auto series = make_series({ "frame_ms" }, 2);
    const auto table  = make_data_table(series);

    // column 4 = frame_ms_sample_count; make_series sets sample_count = i + 1
    EXPECT_EQ(table.rows[0].cells[4], "1");
    EXPECT_EQ(table.rows[1].cells[4], "2");
}

TEST(DiagnosticResampledTimeSeriesTableView, EmptyBucketsProducesValidTable)
{
    const auto series = make_series({ "frame_ms" }, 0);
    const auto table  = make_data_table(series);

    EXPECT_EQ(table.column_count(), 18u);
    EXPECT_EQ(table.row_count(), 0u);
    // Columns present, no rows — still valid per DataTableData::valid()
    EXPECT_TRUE(table.valid());
}
