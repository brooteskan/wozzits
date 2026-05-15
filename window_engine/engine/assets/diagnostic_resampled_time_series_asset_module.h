#pragma once

// engine/assets/diagnostic_resampled_time_series_asset_module.h

#include <asset/system.h>
#include <engine/assets/diagnostic_table_asset_module.h>
#include <engine/assets/diagnostics/diagnostic_resampled_time_series.h>
#include <logging/logger.h>

#include <string>
#include <vector>

namespace wz::engine::assets
{
    struct DiagnosticTimeSeriesResampleDesc
    {
        std::string name;

        DiagnosticTableAsset source{};

        std::string axis_column;
        std::vector<std::string> metric_columns;

        double bucket_width = 0.0;

        bool use_range = false;
        double start = 0.0;
        double end = 0.0;
    };

    struct DiagnosticResampledTimeSeriesAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct DiagnosticResampledTimeSeriesHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    class DiagnosticResampledTimeSeriesAssetModule
    {
    public:
        DiagnosticResampledTimeSeriesAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            DiagnosticResampledTimeSeriesTable& table);

        DiagnosticResampledTimeSeriesAsset create_resampled_time_series(
            const DiagnosticTimeSeriesResampleDesc& desc);

        DiagnosticResampledTimeSeriesHandle get_resampled_time_series(
            const DiagnosticResampledTimeSeriesAsset& asset) const;

        const DiagnosticResampledTimeSeriesData* get_resampled_time_series_data(
            DiagnosticResampledTimeSeriesHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        DiagnosticResampledTimeSeriesTable& table_;
    };
}