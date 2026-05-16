#include <gtest/gtest.h>

#include <engine/assets/diagnostics/diagnostic_timeframe_summary.h>
#include <engine/assets/type_extensions.h>

using namespace wz::engine::assets;

// ─── DiagnosticTimeframeSummaryData::valid() ────────────────────────────────

TEST(DiagnosticTimeframeSummaryData, RejectsMissingFrameColumn)
{
    DiagnosticTimeframeSummaryData data;
    data.metric_columns.push_back("frame_ms");
    data.frame_start = 0;
    data.frame_end   = 99;

    EXPECT_FALSE(data.valid());
}

TEST(DiagnosticTimeframeSummaryData, RejectsMissingMetricColumns)
{
    DiagnosticTimeframeSummaryData data;
    data.frame_column = "frame";
    data.frame_start  = 0;
    data.frame_end    = 99;

    EXPECT_FALSE(data.valid());
}

TEST(DiagnosticTimeframeSummaryData, RejectsInvertedFrameRange)
{
    DiagnosticTimeframeSummaryData data;
    data.frame_column = "frame";
    data.metric_columns.push_back("frame_ms");
    data.frame_start = 100;
    data.frame_end   = 50;

    EXPECT_FALSE(data.valid());
}

TEST(DiagnosticTimeframeSummaryData, AcceptsSingleFrameRange)
{
    DiagnosticTimeframeSummaryData data;
    data.frame_column = "frame";
    data.metric_columns.push_back("frame_ms");
    data.frame_start = 42;
    data.frame_end   = 42;

    EXPECT_TRUE(data.valid());
}

TEST(DiagnosticTimeframeSummaryData, AcceptsZeroFrameRange)
{
    DiagnosticTimeframeSummaryData data;
    data.frame_column = "frame";
    data.metric_columns.push_back("frame_ms");
    data.frame_start = 0;
    data.frame_end   = 0;

    EXPECT_TRUE(data.valid());
}

// ─── DiagnosticTimeframeSummaryTable ─────────────────────────────────────────

TEST(DiagnosticTimeframeSummaryTable, StoresAndRetrievesSummary)
{
    DiagnosticTimeframeSummaryData data;
    data.frame_column = "frame";
    data.metric_columns.push_back("frame_ms");
    data.frame_start = 0;
    data.frame_end   = 99;

    DiagnosticTimeframeSummaryTable table;
    const auto handle = table.add(std::move(data));

    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(handle.type, kAssetTypeDiagnosticTimeframeSummary);

    const auto* stored = table.get(handle);
    ASSERT_NE(stored, nullptr);
    EXPECT_TRUE(stored->valid());
}

TEST(DiagnosticTimeframeSummaryTable, RejectsWrongHandleType)
{
    DiagnosticTimeframeSummaryData data;
    data.frame_column = "frame";
    data.metric_columns.push_back("frame_ms");
    data.frame_start = 0;
    data.frame_end   = 99;

    DiagnosticTimeframeSummaryTable table;
    const auto handle = table.add(std::move(data));
    ASSERT_TRUE(handle.valid());

    wz::asset::ResourceHandle wrong = handle;
    wrong.type = wz::asset::AssetType::Mesh;

    EXPECT_EQ(table.get(wrong), nullptr);
}

// ─── DiagnosticTimeframeBucket::summary_text format ─────────────────────────
//
// summary_text is built by the compiler, not the data struct, so these tests
// verify the format by constructing a bucket manually.

TEST(DiagnosticTimeframeBucket, SummaryTextContainsFrameRange)
{
    DiagnosticTimeframeBucket bucket;
    bucket.frame_start  = 10;
    bucket.frame_end    = 49;
    bucket.row_count    = 37;
    bucket.summary_text = "frames 10-49 rows=37\n  frame_ms: min=1.000 max=5.000 mean=2.500 delta=+1.000\n";

    EXPECT_NE(bucket.summary_text.find("frames 10-49"), std::string::npos);
    EXPECT_NE(bucket.summary_text.find("rows=37"),      std::string::npos);
    EXPECT_NE(bucket.summary_text.find("frame_ms:"),    std::string::npos);
    EXPECT_NE(bucket.summary_text.find("delta="),       std::string::npos);
}
