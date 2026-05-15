#include <gtest/gtest.h>

#include <engine/assets/diagnostics/diagnostic_resampled_time_series.h>
#include <engine/assets/type_extensions.h>

TEST(DiagnosticResampledTimeSeriesData, RejectsMissingAxisColumn)
{
    wz::engine::assets::DiagnosticResampledTimeSeriesData data;
    data.metric_columns.push_back("frame_ms");
    data.bucket_width = 100.0;

    EXPECT_FALSE(data.valid());
}

TEST(DiagnosticResampledTimeSeriesData, RejectsMissingMetricColumns)
{
    wz::engine::assets::DiagnosticResampledTimeSeriesData data;
    data.axis_column = "time_ms";
    data.bucket_width = 100.0;

    EXPECT_FALSE(data.valid());
}

TEST(DiagnosticResampledTimeSeriesData, RejectsNonPositiveBucketWidth)
{
    wz::engine::assets::DiagnosticResampledTimeSeriesData data;
    data.axis_column = "time_ms";
    data.metric_columns.push_back("frame_ms");
    data.bucket_width = 0.0;

    EXPECT_FALSE(data.valid());
}

TEST(DiagnosticResampledTimeSeriesTable, StoresAndRetrievesSeries)
{
    wz::engine::assets::DiagnosticResampledTimeSeriesData data;
    data.axis_column = "time_ms";
    data.metric_columns.push_back("frame_ms");
    data.bucket_width = 100.0;
    data.source_start = 0.0;
    data.source_end = 100.0;

    wz::engine::assets::DiagnosticResampledBucket bucket;
    bucket.start = 0.0;
    bucket.end = 100.0;
    bucket.metrics.push_back({});

    data.buckets.push_back(bucket);

    wz::engine::assets::DiagnosticResampledTimeSeriesTable table;
    const auto handle = table.add(std::move(data));

    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(
        handle.type,
        wz::engine::assets::kAssetTypeDiagnosticResampledTimeSeries);

    const auto* stored = table.get(handle);

    ASSERT_NE(stored, nullptr);
    EXPECT_TRUE(stored->valid());
    EXPECT_EQ(stored->bucket_count(), 1u);
    EXPECT_EQ(stored->metric_count(), 1u);
}

TEST(DiagnosticResampledTimeSeriesTable, RejectsWrongHandleType)
{
    wz::engine::assets::DiagnosticResampledTimeSeriesData data;
    data.axis_column = "time_ms";
    data.metric_columns.push_back("frame_ms");
    data.bucket_width = 100.0;
    data.source_start = 0.0;
    data.source_end = 100.0;

    wz::engine::assets::DiagnosticResampledBucket bucket;
    bucket.start = 0.0;
    bucket.end = 100.0;
    bucket.metrics.push_back({});

    data.buckets.push_back(bucket);

    wz::engine::assets::DiagnosticResampledTimeSeriesTable table;
    const auto handle = table.add(std::move(data));
    ASSERT_TRUE(handle.valid());

    wz::asset::ResourceHandle wrong = handle;
    wrong.type = wz::asset::AssetType::Mesh;

    EXPECT_EQ(table.get(wrong), nullptr);
}