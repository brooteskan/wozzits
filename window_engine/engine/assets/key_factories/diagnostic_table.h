#pragma once

// engine/assets/key_factories/diagnostic_table.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/diagnostics/diagnostic_table.h>

#include <cstdint>
#include <string_view>

namespace wz::engine::assets
{
    [[nodiscard]] inline wz::asset::AssetKey make_inline_diagnostic_table_key(
        std::string_view name,
        const DiagnosticTableData& table) noexcept
    {
        uint64_t h = detail::fnv1a_64(name);

        h = detail::mix64(h, table.schema_version);
        h = detail::mix64(h, static_cast<uint64_t>(table.columns.size()));
        h = detail::mix64(h, static_cast<uint64_t>(table.rows.size()));

        for (const DiagnosticColumn& column : table.columns) {
            h = detail::mix64(h, detail::fnv1a_64(column.name));
        }

        for (const DiagnosticRow& row : table.rows) {
            h = detail::mix64(h, static_cast<uint64_t>(row.cells.size()));

            for (const std::string& cell : row.cells) {
                h = detail::mix64(h, detail::fnv1a_64(cell));
            }
        }

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(kInlineDiagnosticTableSchema.value),
            .compiler_hash = detail::hash_u64(kInlineDiagnosticTableCompilerVersion),
            .deps_hash = {},
        };
    }
}