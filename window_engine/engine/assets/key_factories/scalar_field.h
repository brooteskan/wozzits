#pragma once

// engine/assets/key_factories/scalar_field.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

namespace wz::engine::assets {

    // Key for a scalar field node.
    // Width, height, depth, and format are part of the recipe identity — a field
    // reinterpreted at a different resolution produces a different asset.
    [[nodiscard]] inline wz::asset::AssetKey make_scalar_field_key(
        const wz::asset::AssetKey& source_file_key,
        uint32_t width, uint32_t height, uint32_t depth,
        uint8_t  format_ordinal) noexcept
    {
        const uint64_t dims =
            (static_cast<uint64_t>(width) << 32) |
            (static_cast<uint64_t>(height));
        const uint64_t params =
            (static_cast<uint64_t>(depth) << 8) |
            static_cast<uint64_t>(format_ordinal);

        return wz::asset::AssetKey{
            .content_hash = { detail::mix64(dims, params), 0 },
            .schema_hash = detail::hash_u64(kScalarFieldRawF32Schema.value),
            .compiler_hash = detail::hash_u64(kScalarFieldCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(source_file_key),
        };
    }
}