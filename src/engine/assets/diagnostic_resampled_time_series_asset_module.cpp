// src/engine/assets/diagnostic_resampled_time_series_asset_module.cpp

#include <engine/assets/diagnostic_resampled_time_series_asset_module.h>

#include <engine/assets/key_factories/diagnostic_resampled_time_series.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <vector>

namespace wz::engine::assets
{
    DiagnosticResampledTimeSeriesAssetModule::
        DiagnosticResampledTimeSeriesAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            DiagnosticResampledTimeSeriesTable& table)
        : system_(system)
        , logger_(logger)
        , table_(table)
    {
    }

    DiagnosticResampledTimeSeriesAsset
        DiagnosticResampledTimeSeriesAssetModule::create_resampled_time_series(
            const DiagnosticTimeSeriesResampleDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("diagnostic resampled time series has empty name");
            return {};
        }

        if (!desc.source.valid()) {
            logger_.error(
                "diagnostic resampled time series has invalid source: " +
                desc.name);
            return {};
        }

        if (desc.axis_column.empty()) {
            logger_.error(
                "diagnostic resampled time series has empty axis column: " +
                desc.name);
            return {};
        }

        if (desc.metric_columns.empty()) {
            logger_.error(
                "diagnostic resampled time series has no metric columns: " +
                desc.name);
            return {};
        }

        for (const std::string& metric : desc.metric_columns) {
            if (metric.empty()) {
                logger_.error(
                    "diagnostic resampled time series has empty metric column: " +
                    desc.name);
                return {};
            }
        }

        if (desc.bucket_width <= 0.0) {
            logger_.error(
                "diagnostic resampled time series has non-positive bucket width: " +
                desc.name);
            return {};
        }

        if (desc.use_range && desc.end <= desc.start) {
            logger_.error(
                "diagnostic resampled time series has invalid range: " +
                desc.name);
            return {};
        }

        const wz::asset::AssetKey key =
            make_diagnostic_table_resample_time_series_key(
                desc.name,
                desc.source.output,
                desc.axis_column,
                desc.metric_columns,
                desc.bucket_width,
                desc.use_range,
                desc.start,
                desc.end);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeDiagnosticResampledTimeSeries;
        node.schema = kDiagnosticTableResampleTimeSeriesSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = DiagnosticTimeSeriesResampleCompileDesc{
            .axis_column = desc.axis_column,
            .metric_columns = desc.metric_columns,
            .bucket_width = desc.bucket_width,
            .use_range = desc.use_range,
            .start = desc.start,
            .end = desc.end,
        };

        if (!system_.register_asset(std::move(node), { desc.source.output })) {
            logger_.error(
                "failed to register diagnostic resampled time series: " +
                desc.name);
            return {};
        }

        return DiagnosticResampledTimeSeriesAsset{
            .output = key,
        };
    }

    DiagnosticResampledTimeSeriesHandle
        DiagnosticResampledTimeSeriesAssetModule::get_resampled_time_series(
            const DiagnosticResampledTimeSeriesAsset& asset) const
    {
        if (!asset.valid())
            return {};

        DiagnosticResampledTimeSeriesHandle out{};

        if (const auto* compiled = system_.find_compiled(asset.output))
            out.handle = compiled->handle;

        if (!out.valid())
            logger_.error("diagnostic resampled time series handle not found");

        return out;
    }

    const DiagnosticResampledTimeSeriesData*
        DiagnosticResampledTimeSeriesAssetModule::get_resampled_time_series_data(
            DiagnosticResampledTimeSeriesHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return table_.get(handle.handle);
    }
}