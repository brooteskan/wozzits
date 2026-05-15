#pragma once

// engine/assets/key_factories/diagnostic_resampled_time_series.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>
#include <string>

namespace wz::engine::assets
{
    namespace diagnostic_resample_key_detail
    {
        [[nodiscard]] inline uint64_t double_to_u64_bits(double v) noexcept
        {
            uint64_t bits = 0;
            static_assert(sizeof(bits) == sizeof(v));
            std::memcpy(&bits, &v, sizeof(bits));
            return bits;
        }
    }

    [[nodiscard]] inline wz::asset::AssetKey
        make_diagnostic_table_resample_time_series_key(
            std::string_view name,
            const wz::asset::AssetKey& source_table_key,
            std::string_view axis_column,
            const std::vector<std::string>& metric_columns,
            double bucket_width,
            bool use_range,
            double start,
            double end) noexcept
    {
        uint64_t h = detail::fnv1a_64(name);

        h = detail::mix64(h, detail::fnv1a_64(axis_column));
        h = detail::mix64(
            h,
            diagnostic_resample_key_detail::double_to_u64_bits(bucket_width));

        h = detail::mix64(h, use_range ? 1ull : 0ull);

        if (use_range) {
            h = detail::mix64(
                h,
                diagnostic_resample_key_detail::double_to_u64_bits(start));
            h = detail::mix64(
                h,
                diagnostic_resample_key_detail::double_to_u64_bits(end));
        }

        h = detail::mix64(h, static_cast<uint64_t>(metric_columns.size()));

        for (const std::string& metric : metric_columns) {
            h = detail::mix64(h, detail::fnv1a_64(metric));
        }

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash =
                detail::hash_u64(kDiagnosticTableResampleTimeSeriesSchema.value),
            .compiler_hash =
                detail::hash_u64(kDiagnosticTableResampleTimeSeriesCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(source_table_key),
        };
    }
}