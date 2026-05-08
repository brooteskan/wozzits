#pragma once

// engine/assets/key_factories/csv.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

namespace wz::engine::assets {

    // Key for a CSV table recipe node.
    //
    // Identity encodes the source file dependency and the header_mode ordinal.
    // The same CSV file compiled with different header modes produces distinct
    // asset keys and distinct CSVData (because has_header and the row split differ).
    //
    // source_file_key — key of the kCSVFileSchema carrier node
    // header_mode     — cast from CSVHeaderMode
    [[nodiscard]] inline wz::asset::AssetKey make_csv_key(
        const wz::asset::AssetKey& source_file_key,
        uint8_t                    header_mode_ordinal) noexcept
    {
        return wz::asset::AssetKey{
            .content_hash  = { static_cast<uint64_t>(header_mode_ordinal), 0 },
            .schema_hash   = detail::hash_u64(kCSVTableSchema.value),
            .compiler_hash = detail::hash_u64(kCSVCompilerVersion),
            .deps_hash     = detail::key_to_dep_hash(source_file_key),
        };
    }

} // namespace wz::engine::assets
