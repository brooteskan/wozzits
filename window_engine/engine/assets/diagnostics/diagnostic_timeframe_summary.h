#pragma once

// engine/assets/diagnostics/diagnostic_timeframe_summary.h

#include <asset/types.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wz::engine::assets
{
    struct DiagnosticTimeframeMetricStats
    {
        uint32_t sample_count = 0;

        double min   = 0.0;
        double max   = 0.0;
        double mean  = 0.0;
        double first = 0.0;
        double last  = 0.0;
        double delta = 0.0;  // last - first
    };

    struct DiagnosticTimeframeBucket
    {
        uint32_t frame_start = 0;
        uint32_t frame_end   = 0;
        uint32_t row_count   = 0;

        std::vector<DiagnosticTimeframeMetricStats> metrics;

        // Deterministic human/agent-readable summary of this bucket.
        // Format:
        //   frames <start>-<end> rows=<n>
        //   <metric>: min=<v> max=<v> mean=<v> delta=<+/-v>
        //   ...
        std::string summary_text;
    };

    struct DiagnosticTimeframeSummaryData
    {
        std::string              frame_column;
        std::vector<std::string> metric_columns;

        uint32_t frame_start      = 0;
        uint32_t frame_end        = 0;
        uint32_t frames_per_bucket = 0;  // 0 = entire range is one bucket

        std::vector<DiagnosticTimeframeBucket> buckets;

        bool     valid()        const noexcept;
        uint32_t bucket_count() const noexcept;
        uint32_t metric_count() const noexcept;
    };

    class DiagnosticTimeframeSummaryTable
    {
    public:
        DiagnosticTimeframeSummaryTable();

        wz::asset::ResourceHandle add(DiagnosticTimeframeSummaryData data);
        const DiagnosticTimeframeSummaryData* get(
            wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        std::vector<DiagnosticTimeframeSummaryData> summaries_;
        std::vector<uint32_t>                       epochs_;
    };

    struct DiagnosticTimeframeSummaryCompileDesc
    {
        std::string              frame_column;
        std::vector<std::string> metric_columns;
        uint32_t frame_start       = 0;
        uint32_t frame_end         = 0;
        uint32_t frames_per_bucket = 0;
    };
}
