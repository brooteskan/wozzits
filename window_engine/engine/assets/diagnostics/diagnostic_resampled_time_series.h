#pragma once

// engine/assets/diagnostics/diagnostic_resampled_time_series.h

#include <asset/types.h>
#include <engine/assets/diagnostics/diagnostic_table.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wz::engine::assets
{
    struct DiagnosticMetricBucketStats
    {
        uint32_t sample_count = 0;

        double min = 0.0;
        double max = 0.0;
        double mean = 0.0;
        double first = 0.0;
        double last = 0.0;

        double min_axis = 0.0;
        double max_axis = 0.0;
        double first_axis = 0.0;
        double last_axis = 0.0;

        uint32_t min_source_row = 0;
        uint32_t max_source_row = 0;
        uint32_t first_source_row = 0;
        uint32_t last_source_row = 0;
    };

    struct DiagnosticResampledBucket
    {
        double start = 0.0;
        double end = 0.0;

        uint32_t source_row_count = 0;

        std::vector<DiagnosticMetricBucketStats> metrics;
    };

    struct DiagnosticResampledTimeSeriesData
    {
        std::string axis_column;
        std::vector<std::string> metric_columns;

        double bucket_width = 0.0;

        double source_start = 0.0;
        double source_end = 0.0;

        uint32_t source_row_count = 0;
        uint32_t parsed_row_count = 0;
        uint32_t skipped_row_count = 0;

        std::vector<DiagnosticResampledBucket> buckets;

        bool valid() const noexcept;
        uint32_t bucket_count() const noexcept;
        uint32_t metric_count() const noexcept;
    };

    class DiagnosticResampledTimeSeriesTable
    {
    public:
        DiagnosticResampledTimeSeriesTable();

        wz::asset::ResourceHandle add(DiagnosticResampledTimeSeriesData series);
        const DiagnosticResampledTimeSeriesData* get(
            wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        std::vector<DiagnosticResampledTimeSeriesData> series_;
        std::vector<uint32_t> epochs_;
    };

    //struct DiagnosticTimeSeriesResampleDesc
    //{
    //    std::string name;

    //    DiagnosticTableAsset source{};

    //    std::string axis_column;
    //    std::vector<std::string> metric_columns;

    //    double bucket_width = 0.0;

    //    // If false, the compiler uses the min/max axis values found in the
    //    // parsed source rows.
    //    bool use_range = false;
    //    double start = 0.0;
    //    double end = 0.0;
    //};

    struct DiagnosticTimeSeriesResampleCompileDesc
    {
        std::string axis_column;
        std::vector<std::string> metric_columns;

        double bucket_width = 0.0;

        bool use_range = false;
        double start = 0.0;
        double end = 0.0;
    };
}