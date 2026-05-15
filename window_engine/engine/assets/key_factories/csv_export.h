#pragma once

// engine/assets/key_factories/csv_export.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

#include <cstdint>
#include <string_view>

namespace wz::engine::assets
{
    [[nodiscard]] inline wz::asset::AssetKey make_csv_export_key(
        std::string_view name,
        const wz::asset::AssetKey& source_table_key,
        char separator,
        bool include_header) noexcept
    {
        uint64_t h = detail::fnv1a_64(name);

        h = detail::mix64(h, static_cast<uint64_t>(static_cast<uint8_t>(separator)));
        h = detail::mix64(h, include_header ? 1ull : 0ull);

        return wz::asset::AssetKey{
            .content_hash  = detail::hash_u64(h),
            .schema_hash   = detail::hash_u64(kCSVExportSchema.value),
            .compiler_hash = detail::hash_u64(kCSVExportCompilerVersion),
            .deps_hash     = detail::key_to_dep_hash(source_table_key),
        };
    }
}
