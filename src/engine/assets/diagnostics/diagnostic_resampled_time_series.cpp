// src/engine/assets/diagnostics/diagnostic_resampled_time_series.cpp

#include <engine/assets/diagnostics/diagnostic_resampled_time_series.h>
#include <engine/assets/type_extensions.h>

#include <string>
#include <utility>

namespace wz::engine::assets
{
    bool DiagnosticResampledTimeSeriesData::valid() const noexcept
    {
        if (axis_column.empty())
            return false;

        if (metric_columns.empty())
            return false;

        if (bucket_width <= 0.0)
            return false;

        for (const std::string& metric : metric_columns) {
            if (metric.empty())
                return false;
        }

        for (const DiagnosticResampledBucket& bucket : buckets) {
            if (bucket.end <= bucket.start)
                return false;

            if (bucket.metrics.size() != metric_columns.size())
                return false;
        }

        return true;
    }

    uint32_t DiagnosticResampledTimeSeriesData::bucket_count() const noexcept
    {
        return static_cast<uint32_t>(buckets.size());
    }

    uint32_t DiagnosticResampledTimeSeriesData::metric_count() const noexcept
    {
        return static_cast<uint32_t>(metric_columns.size());
    }

    DiagnosticResampledTimeSeriesTable::DiagnosticResampledTimeSeriesTable()
    {
        series_.emplace_back();
        epochs_.push_back(0);
    }

    wz::asset::ResourceHandle DiagnosticResampledTimeSeriesTable::add(
        DiagnosticResampledTimeSeriesData series)
    {
        if (!series.valid())
            return {};

        const uint32_t id = static_cast<uint32_t>(series_.size());

        series_.push_back(std::move(series));
        epochs_.push_back(1);

        return wz::asset::ResourceHandle{
            .id = id,
            .epoch = epochs_[id],
            .type = kAssetTypeDiagnosticResampledTimeSeries,
        };
    }

    const DiagnosticResampledTimeSeriesData*
        DiagnosticResampledTimeSeriesTable::get(
            wz::asset::ResourceHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        if (handle.type != kAssetTypeDiagnosticResampledTimeSeries)
            return nullptr;

        if (handle.id >= series_.size())
            return nullptr;

        if (epochs_[handle.id] != handle.epoch)
            return nullptr;

        return &series_[handle.id];
    }

    void DiagnosticResampledTimeSeriesTable::destroy()
    {
        series_.clear();
        epochs_.clear();

        series_.emplace_back();
        epochs_.push_back(0);
    }

    DataTableData make_data_table(const DiagnosticResampledTimeSeriesData& series)
    {
        DataTableData out;

        out.columns.push_back({ .name = "bucket_index" });
        out.columns.push_back({ .name = "bucket_start" });
        out.columns.push_back({ .name = "bucket_end" });
        out.columns.push_back({ .name = "source_row_count" });

        for (const std::string& metric : series.metric_columns) {
            out.columns.push_back({ .name = metric + "_sample_count" });
            out.columns.push_back({ .name = metric + "_min" });
            out.columns.push_back({ .name = metric + "_max" });
            out.columns.push_back({ .name = metric + "_mean" });
            out.columns.push_back({ .name = metric + "_first" });
            out.columns.push_back({ .name = metric + "_last" });
            out.columns.push_back({ .name = metric + "_min_axis" });
            out.columns.push_back({ .name = metric + "_max_axis" });
            out.columns.push_back({ .name = metric + "_first_axis" });
            out.columns.push_back({ .name = metric + "_last_axis" });
            out.columns.push_back({ .name = metric + "_min_source_row" });
            out.columns.push_back({ .name = metric + "_max_source_row" });
            out.columns.push_back({ .name = metric + "_first_source_row" });
            out.columns.push_back({ .name = metric + "_last_source_row" });
        }

        for (uint32_t i = 0; i < series.bucket_count(); ++i) {
            const DiagnosticResampledBucket& bucket = series.buckets[i];

            DataTableRow row;
            row.cells.reserve(out.columns.size());

            row.cells.push_back(std::to_string(i));
            row.cells.push_back(std::to_string(bucket.start));
            row.cells.push_back(std::to_string(bucket.end));
            row.cells.push_back(std::to_string(bucket.source_row_count));

            for (const DiagnosticMetricBucketStats& stats : bucket.metrics) {
                row.cells.push_back(std::to_string(stats.sample_count));
                row.cells.push_back(std::to_string(stats.min));
                row.cells.push_back(std::to_string(stats.max));
                row.cells.push_back(std::to_string(stats.mean));
                row.cells.push_back(std::to_string(stats.first));
                row.cells.push_back(std::to_string(stats.last));
                row.cells.push_back(std::to_string(stats.min_axis));
                row.cells.push_back(std::to_string(stats.max_axis));
                row.cells.push_back(std::to_string(stats.first_axis));
                row.cells.push_back(std::to_string(stats.last_axis));
                row.cells.push_back(std::to_string(stats.min_source_row));
                row.cells.push_back(std::to_string(stats.max_source_row));
                row.cells.push_back(std::to_string(stats.first_source_row));
                row.cells.push_back(std::to_string(stats.last_source_row));
            }

            out.rows.push_back(std::move(row));
        }

        return out;
    }
}