// src/engine/assets/diagnostics/diagnostic_timeframe_summary_compilers.cpp

#include <engine/assets/diagnostics/diagnostic_timeframe_summary_compilers.h>

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <any>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        bool parse_frame_index(const std::string& text, uint32_t& out)
        {
            errno = 0;

            const char* begin = text.c_str();
            char* end         = nullptr;

            const long long value = std::strtoll(begin, &end, 10);

            if (begin == end)
                return false;

            if (errno == ERANGE)
                return false;

            while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')
                ++end;

            if (*end != '\0')
                return false;

            if (value < 0)
                return false;

            out = static_cast<uint32_t>(value);
            return true;
        }

        bool parse_double_strict(const std::string& text, double& out)
        {
            errno = 0;

            const char* begin = text.c_str();
            char* end         = nullptr;

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

        int find_column_index(const DataTableData& table, const std::string& name)
        {
            for (uint32_t i = 0; i < static_cast<uint32_t>(table.columns.size()); ++i) {
                if (table.columns[i].name == name)
                    return static_cast<int>(i);
            }
            return -1;
        }

        struct MetricAccumulator
        {
            bool has_value = false;

            uint32_t sample_count = 0;
            double sum   = 0.0;
            double min   = 0.0;
            double max   = 0.0;
            double first = 0.0;
            double last  = 0.0;

            void add(double value)
            {
                if (!has_value) {
                    has_value     = true;
                    sample_count  = 1;
                    sum = min = max = first = last = value;
                    return;
                }

                ++sample_count;
                sum += value;

                if (value < min) min = value;
                if (value > max) max = value;

                last = value;
            }

            DiagnosticTimeframeMetricStats finish() const
            {
                DiagnosticTimeframeMetricStats out{};

                if (!has_value)
                    return out;

                out.sample_count = sample_count;
                out.min          = min;
                out.max          = max;
                out.mean         = sum / static_cast<double>(sample_count);
                out.first        = first;
                out.last         = last;
                out.delta        = last - first;

                return out;
            }
        };

        std::string build_bucket_summary_text(
            uint32_t frame_start,
            uint32_t frame_end,
            uint32_t row_count,
            const std::vector<std::string>& metric_columns,
            const std::vector<DiagnosticTimeframeMetricStats>& metrics)
        {
            char buf[128];

            std::string text;
            std::snprintf(buf, sizeof(buf),
                "frames %u-%u rows=%u\n", frame_start, frame_end, row_count);
            text += buf;

            for (size_t i = 0; i < metric_columns.size(); ++i) {
                const DiagnosticTimeframeMetricStats& s = metrics[i];

                std::snprintf(buf, sizeof(buf),
                    "  %s: min=%.3f max=%.3f mean=%.3f delta=%+.3f\n",
                    metric_columns[i].c_str(),
                    s.min, s.max, s.mean, s.delta);

                text += buf;
            }

            return text;
        }

        DiagnosticTimeframeSummaryData summarize(
            const DataTableData& source,
            const DiagnosticTimeframeSummaryCompileDesc& desc)
        {
            DiagnosticTimeframeSummaryData out{};
            out.frame_column     = desc.frame_column;
            out.metric_columns   = desc.metric_columns;
            out.frame_start      = desc.frame_start;
            out.frame_end        = desc.frame_end;
            out.frames_per_bucket = desc.frames_per_bucket;

            const int frame_col_index = find_column_index(source, desc.frame_column);
            if (frame_col_index < 0)
                return {};

            std::vector<int> metric_indices;
            metric_indices.reserve(desc.metric_columns.size());

            for (const std::string& metric : desc.metric_columns) {
                const int idx = find_column_index(source, metric);
                if (idx < 0)
                    return {};
                metric_indices.push_back(idx);
            }

            const uint32_t range = desc.frame_end - desc.frame_start;
            const uint32_t fpb   = desc.frames_per_bucket;

            const uint32_t bucket_count =
                (fpb == 0)
                    ? 1u
                    : (range / fpb) + ((range % fpb != 0) ? 1u : 0u) +
                      (range == 0 ? 1u : 0u);

            struct BucketAccum
            {
                uint32_t row_count = 0;
                std::vector<MetricAccumulator> metrics;
            };

            std::vector<BucketAccum> accums(bucket_count);
            for (BucketAccum& b : accums)
                b.metrics.resize(desc.metric_columns.size());

            for (const DataTableRow& row : source.rows) {
                if (row.cells.size() != source.columns.size())
                    continue;

                uint32_t frame = 0;
                if (!parse_frame_index(row.cells[frame_col_index], frame))
                    continue;

                if (frame < desc.frame_start || frame > desc.frame_end)
                    continue;

                const uint32_t bucket_index =
                    (fpb == 0)
                        ? 0u
                        : std::min(
                            (frame - desc.frame_start) / fpb,
                            bucket_count - 1u);

                BucketAccum& b = accums[bucket_index];
                ++b.row_count;

                for (size_t mi = 0; mi < metric_indices.size(); ++mi) {
                    double value = 0.0;
                    if (!parse_double_strict(row.cells[metric_indices[mi]], value))
                        continue;
                    b.metrics[mi].add(value);
                }
            }

            out.buckets.reserve(bucket_count);

            for (uint32_t bi = 0; bi < bucket_count; ++bi) {
                const BucketAccum& b = accums[bi];

                DiagnosticTimeframeBucket bucket{};

                if (fpb == 0) {
                    bucket.frame_start = desc.frame_start;
                    bucket.frame_end   = desc.frame_end;
                }
                else {
                    bucket.frame_start = desc.frame_start + bi * fpb;
                    bucket.frame_end   = std::min(
                        bucket.frame_start + fpb - 1u,
                        desc.frame_end);
                }

                bucket.row_count = b.row_count;
                bucket.metrics.reserve(desc.metric_columns.size());

                for (const MetricAccumulator& acc : b.metrics)
                    bucket.metrics.push_back(acc.finish());

                bucket.summary_text = build_bucket_summary_text(
                    bucket.frame_start,
                    bucket.frame_end,
                    bucket.row_count,
                    desc.metric_columns,
                    bucket.metrics);

                out.buckets.push_back(std::move(bucket));
            }

            return out;
        }
    }

    void register_diagnostic_timeframe_summary_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        DataTable& data_table,
        DiagnosticTimeframeSummaryTable& summary_table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kDiagnosticTimeframeSummarySchema,
            .output_type  = kAssetTypeDiagnosticTimeframeSummary,
            .compile = [&logger, &data_table, &summary_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<DiagnosticTimeframeSummaryCompileDesc>(
                        &input.meta);

                if (!desc) {
                    logger.error("timeframe summary missing compile desc");
                    return compile_failed_node(input);
                }

                if (dep_nodes.size() != 1 || dep_handles.size() != 1) {
                    logger.error(
                        "timeframe summary requires exactly one source table dependency");
                    return compile_failed_node(input);
                }

                if (desc->frame_column.empty()) {
                    logger.error("timeframe summary has empty frame column");
                    return compile_failed_node(input);
                }

                if (desc->metric_columns.empty()) {
                    logger.error("timeframe summary has no metric columns");
                    return compile_failed_node(input);
                }

                if (desc->frame_end < desc->frame_start) {
                    logger.error("timeframe summary has inverted frame range");
                    return compile_failed_node(input);
                }

                const DataTableData* source = data_table.get(dep_handles[0]);

                if (!source || !source->valid()) {
                    logger.error("timeframe summary source table is invalid");
                    return compile_failed_node(input);
                }

                DiagnosticTimeframeSummaryData data = summarize(*source, *desc);

                if (!data.valid()) {
                    logger.error("timeframe summary produced invalid data — "
                                 "check frame or metric column names");
                    return compile_failed_node(input);
                }

                wz::asset::ResourceHandle handle =
                    summary_table.add(std::move(data));

                if (!handle.valid()) {
                    logger.error("failed to store timeframe summary");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage   = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
        });
    }
}
