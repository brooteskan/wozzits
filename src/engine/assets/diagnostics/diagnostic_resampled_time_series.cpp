// src/engine/assets/diagnostics/diagnostic_resampled_time_series.cpp

#include <engine/assets/diagnostics/diagnostic_resampled_time_series.h>
#include <engine/assets/type_extensions.h>

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
}