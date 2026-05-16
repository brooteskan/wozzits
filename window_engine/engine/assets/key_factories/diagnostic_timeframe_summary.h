#pragma once

// engine/assets/key_factories/diagnostic_timeframe_summary.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

#include <cstdint>
#include <string_view>
#include <vector>
#include <string>

namespace wz::engine::assets
{
    [[nodiscard]] inline wz::asset::AssetKey make_diagnostic_timeframe_summary_key(
        std::string_view name,
        const wz::asset::AssetKey& source_table_key,
        std::string_view frame_column,
        const std::vector<std::string>& metric_columns,
        uint32_t frame_start,
        uint32_t frame_end,
        uint32_t frames_per_bucket) noexcept
    {
        uint64_t h = detail::fnv1a_64(name);

        h = detail::mix64(h, detail::fnv1a_64(frame_column));
        h = detail::mix64(h, static_cast<uint64_t>(frame_start));
        h = detail::mix64(h, static_cast<uint64_t>(frame_end));
        h = detail::mix64(h, static_cast<uint64_t>(frames_per_bucket));
        h = detail::mix64(h, static_cast<uint64_t>(metric_columns.size()));

        for (const std::string& metric : metric_columns)
            h = detail::mix64(h, detail::fnv1a_64(metric));

        return wz::asset::AssetKey{
            .content_hash  = detail::hash_u64(h),
            .schema_hash   = detail::hash_u64(kDiagnosticTimeframeSummarySchema.value),
            .compiler_hash = detail::hash_u64(kDiagnosticTimeframeSummaryCompilerVersion),
            .deps_hash     = detail::key_to_dep_hash(source_table_key),
        };
    }
}
