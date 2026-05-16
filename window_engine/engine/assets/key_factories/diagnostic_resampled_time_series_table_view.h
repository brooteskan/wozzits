#pragma once

// engine/assets/key_factories/diagnostic_resampled_time_series_table_view.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

#include <string_view>

namespace wz::engine::assets
{
    [[nodiscard]] inline wz::asset::AssetKey
        make_diagnostic_resampled_time_series_table_view_key(
            std::string_view name,
            const wz::asset::AssetKey& source_series_key) noexcept
    {
        const uint64_t h = detail::fnv1a_64(name);

        return wz::asset::AssetKey{
            .content_hash  = detail::hash_u64(h),
            .schema_hash   = detail::hash_u64(
                kDiagnosticResampledTimeSeriesToDataTableSchema.value),
            .compiler_hash = detail::hash_u64(
                kDiagnosticResampledTimeSeriesToDataTableCompilerVersion),
            .deps_hash     = detail::key_to_dep_hash(source_series_key),
        };
    }
}
