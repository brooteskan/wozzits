// src/engine/assets/diagnostics/diagnostic_resampled_time_series_compilers.cpp

#include <engine/assets/diagnostics/diagnostic_resampled_time_series_compilers.h>

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <any>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <span>
#include <string>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        bool parse_double_strict(const std::string& text, double& out)
        {
            errno = 0;

            const char* begin = text.c_str();
            char* end = nullptr;

            const double value = std::strtod(begin, &end);

            if (begin == end)
                return false;

            if (errno == ERANGE)
                return false;

            while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')
                ++end;

            if (*end != '\0')
                return false;

            out = value;
            return true;
        }

        int find_column_index(
            const DataTableData& table,
            const std::string& name)
        {
            for (uint32_t i = 0;
                i < static_cast<uint32_t>(table.columns.size());
                ++i)
            {
                if (table.columns[i].name == name)
                    return static_cast<int>(i);
            }

            return -1;
        }

        struct Observation
        {
            double axis = 0.0;
            uint32_t source_row = 0;
            std::vector<double> values;
        };

        struct Accumulator
        {
            bool has_value = false;

            uint32_t sample_count = 0;

            double sum = 0.0;

            double min = 0.0;
            double max = 0.0;
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

            void add(double value, double axis, uint32_t source_row)
            {
                if (!has_value) {
                    has_value = true;

                    sample_count = 1;
                    sum = value;

                    min = value;
                    max = value;
                    first = value;
                    last = value;

                    min_axis = axis;
                    max_axis = axis;
                    first_axis = axis;
                    last_axis = axis;

                    min_source_row = source_row;
                    max_source_row = source_row;
                    first_source_row = source_row;
                    last_source_row = source_row;
                    return;
                }

                ++sample_count;
                sum += value;

                if (axis < first_axis) {
                    first = value;
                    first_axis = axis;
                    first_source_row = source_row;
                }

                if (axis >= last_axis) {
                    last = value;
                    last_axis = axis;
                    last_source_row = source_row;
                }

                if (value < min) {
                    min = value;
                    min_axis = axis;
                    min_source_row = source_row;
                }

                if (value > max) {
                    max = value;
                    max_axis = axis;
                    max_source_row = source_row;
                }
            }

            DiagnosticMetricBucketStats finish() const
            {
                DiagnosticMetricBucketStats out{};

                if (!has_value)
                    return out;

                out.sample_count = sample_count;
                out.min = min;
                out.max = max;
                out.mean = sum / static_cast<double>(sample_count);
                out.first = first;
                out.last = last;

                out.min_axis = min_axis;
                out.max_axis = max_axis;
                out.first_axis = first_axis;
                out.last_axis = last_axis;

                out.min_source_row = min_source_row;
                out.max_source_row = max_source_row;
                out.first_source_row = first_source_row;
                out.last_source_row = last_source_row;

                return out;
            }
        };

        DiagnosticResampledTimeSeriesData resample_table(
            const DataTableData& table,
            const DiagnosticTimeSeriesResampleCompileDesc& desc,
            uint32_t& skipped_rows_out)
        {
            skipped_rows_out = 0;

            DiagnosticResampledTimeSeriesData out{};
            out.axis_column = desc.axis_column;
            out.metric_columns = desc.metric_columns;
            out.bucket_width = desc.bucket_width;
            out.source_row_count = table.row_count();

            const int axis_index =
                find_column_index(table, desc.axis_column);

            if (axis_index < 0)
                return {};

            std::vector<int> metric_indices;
            metric_indices.reserve(desc.metric_columns.size());

            for (const std::string& metric : desc.metric_columns) {
                const int index = find_column_index(table, metric);

                if (index < 0)
                    return {};

                metric_indices.push_back(index);
            }

            std::vector<Observation> observations;
            observations.reserve(table.rows.size());

            double observed_min_axis = 0.0;
            double observed_max_axis = 0.0;
            bool have_axis = false;

            for (uint32_t row_index = 0;
                row_index < static_cast<uint32_t>(table.rows.size());
                ++row_index)
            {
                const DataTableRow& row = table.rows[row_index];

                if (row.cells.size() != table.columns.size()) {
                    ++skipped_rows_out;
                    continue;
                }

                double axis = 0.0;

                if (!parse_double_strict(row.cells[axis_index], axis)) {
                    ++skipped_rows_out;
                    continue;
                }

                if (desc.use_range) {
                    if (axis < desc.start || axis > desc.end) {
                        continue;
                    }
                }

                Observation obs{};
                obs.axis = axis;
                obs.source_row = row_index;
                obs.values.reserve(metric_indices.size());

                bool row_ok = true;

                for (const int metric_index : metric_indices) {
                    double value = 0.0;

                    if (!parse_double_strict(row.cells[metric_index], value)) {
                        row_ok = false;
                        break;
                    }

                    obs.values.push_back(value);
                }

                if (!row_ok) {
                    ++skipped_rows_out;
                    continue;
                }

                if (!have_axis) {
                    have_axis = true;
                    observed_min_axis = axis;
                    observed_max_axis = axis;
                }
                else {
                    observed_min_axis = std::min(observed_min_axis, axis);
                    observed_max_axis = std::max(observed_max_axis, axis);
                }

                observations.push_back(std::move(obs));
            }

            out.parsed_row_count =
                static_cast<uint32_t>(observations.size());
            out.skipped_row_count = skipped_rows_out;

            if (observations.empty())
                return out;

            double range_start =
                desc.use_range ? desc.start : observed_min_axis;

            double range_end =
                desc.use_range ? desc.end : observed_max_axis;

            if (range_end <= range_start)
                range_end = range_start + desc.bucket_width;

            out.source_start = range_start;
            out.source_end = range_end;

            const double range_width = range_end - range_start;

            uint32_t bucket_count =
                static_cast<uint32_t>(
                    std::ceil(range_width / desc.bucket_width));

            if (bucket_count == 0)
                bucket_count = 1;

            struct BucketAccumulator
            {
                uint32_t source_row_count = 0;
                std::vector<Accumulator> metrics;
            };

            std::vector<BucketAccumulator> accumulators;
            accumulators.resize(bucket_count);

            for (BucketAccumulator& bucket : accumulators) {
                bucket.metrics.resize(desc.metric_columns.size());
            }

            for (const Observation& obs : observations) {
                if (obs.axis < range_start || obs.axis > range_end)
                    continue;

                uint32_t bucket_index = 0;

                if (obs.axis == range_end) {
                    bucket_index = bucket_count - 1;
                }
                else {
                    const double relative = obs.axis - range_start;
                    bucket_index =
                        static_cast<uint32_t>(
                            std::floor(relative / desc.bucket_width));

                    if (bucket_index >= bucket_count)
                        bucket_index = bucket_count - 1;
                }

                BucketAccumulator& bucket = accumulators[bucket_index];
                ++bucket.source_row_count;

                for (uint32_t metric_i = 0;
                    metric_i < static_cast<uint32_t>(obs.values.size());
                    ++metric_i)
                {
                    bucket.metrics[metric_i].add(
                        obs.values[metric_i],
                        obs.axis,
                        obs.source_row);
                }
            }

            out.buckets.reserve(bucket_count);

            for (uint32_t bucket_i = 0; bucket_i < bucket_count; ++bucket_i) {
                DiagnosticResampledBucket bucket{};
                bucket.start =
                    range_start +
                    static_cast<double>(bucket_i) * desc.bucket_width;
                bucket.end = bucket.start + desc.bucket_width;

                if (bucket_i + 1 == bucket_count)
                    bucket.end = range_end;

                bucket.source_row_count =
                    accumulators[bucket_i].source_row_count;

                bucket.metrics.reserve(desc.metric_columns.size());

                for (const Accumulator& acc : accumulators[bucket_i].metrics) {
                    bucket.metrics.push_back(acc.finish());
                }

                out.buckets.push_back(std::move(bucket));
            }

            return out;
        }
    }

    void register_diagnostic_resampled_time_series_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        DataTable& data_table,
        DiagnosticResampledTimeSeriesTable& resampled_table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kDiagnosticResampledTimeSeriesToDataTableSchema,
            .output_type  = kAssetTypeDataTable,
            .compile = [&logger, &data_table, &resampled_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                if (!std::any_cast<DiagnosticResampledTimeSeriesToDataTableCompileDesc>(
                        &input.meta))
                {
                    logger.error(
                        "resampled time series table view missing compile desc");
                    return compile_failed_node(input);
                }

                if (dep_nodes.size() != 1 || dep_handles.size() != 1) {
                    logger.error(
                        "resampled time series table view requires exactly one "
                        "source dependency");
                    return compile_failed_node(input);
                }

                const DiagnosticResampledTimeSeriesData* source =
                    resampled_table.get(dep_handles[0]);

                if (!source || !source->valid()) {
                    logger.error(
                        "resampled time series table view source is invalid");
                    return compile_failed_node(input);
                }

                DataTableData table_data = make_data_table(*source);

                if (!table_data.valid()) {
                    logger.error(
                        "resampled time series table view produced invalid table");
                    return compile_failed_node(input);
                }

                wz::asset::ResourceHandle handle =
                    data_table.add(std::move(table_data));

                if (!handle.valid()) {
                    logger.error(
                        "failed to store resampled time series table view");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage   = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
        });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kDiagnosticTableResampleTimeSeriesSchema,
            .output_type = kAssetTypeDiagnosticResampledTimeSeries,
            .compile = [&logger, &data_table, &resampled_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<DiagnosticTimeSeriesResampleCompileDesc>(
                        &input.meta);

                if (!desc) {
                    logger.error(
                        "diagnostic resampled time series missing compile desc");
                    return compile_failed_node(input);
                }

                if (dep_nodes.size() != 1 || dep_handles.size() != 1) {
                    logger.error(
                        "diagnostic resampled time series requires one source table dependency");
                    return compile_failed_node(input);
                }

                if (desc->axis_column.empty()) {
                    logger.error(
                        "diagnostic resampled time series has empty axis column");
                    return compile_failed_node(input);
                }

                if (desc->metric_columns.empty()) {
                    logger.error(
                        "diagnostic resampled time series has no metric columns");
                    return compile_failed_node(input);
                }

                if (desc->bucket_width <= 0.0) {
                    logger.error(
                        "diagnostic resampled time series has non-positive bucket width");
                    return compile_failed_node(input);
                }

                if (desc->use_range && desc->end <= desc->start) {
                    logger.error(
                        "diagnostic resampled time series has invalid range");
                    return compile_failed_node(input);
                }

                const DataTableData* source =
                    data_table.get(dep_handles[0]);

                if (!source || !source->valid()) {
                    logger.error(
                        "diagnostic resampled time series source table is invalid");
                    return compile_failed_node(input);
                }

                uint32_t skipped_rows = 0;

                DiagnosticResampledTimeSeriesData data =
                    resample_table(*source, *desc, skipped_rows);

                if (!data.valid()) {
                    logger.error(
                        "diagnostic resampled time series produced invalid data");
                    return compile_failed_node(input);
                }

                wz::asset::ResourceHandle handle =
                    resampled_table.add(std::move(data));

                if (!handle.valid()) {
                    logger.error(
                        "failed to store diagnostic resampled time series");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });
    }
}