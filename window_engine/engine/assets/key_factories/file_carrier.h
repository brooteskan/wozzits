#pragma once

// engine/assets/key_factories/file_carrier.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

#include <span>

namespace wz::engine::assets {
    // Key for a file carrier node (raw bytes, no transformation).
    // canonical_path must be relative to the resource root and normalised
    // (forward slashes, no leading slash) so that keys are portable. This
    // overload is the path-only fallback for callers that cannot read bytes.
    [[nodiscard]] inline wz::asset::AssetKey make_file_key(
        std::string_view canonical_path,
        wz::asset::SchemaID schema) noexcept
    {
        return wz::asset::AssetKey{
            .content_hash = detail::hash_str(canonical_path),
            .schema_hash = detail::hash_u64(schema.value),
            .compiler_hash = {},   // carriers perform no transformation
            .deps_hash = {},   // file nodes have no DAG prerequisites
        };
    }

    [[nodiscard]] inline wz::asset::AssetKey make_file_content_key(
        std::string_view canonical_path,
        std::span<const std::uint8_t> bytes,
        wz::asset::SchemaID schema) noexcept
    {
        const wz::asset::Hash path_hash = detail::hash_str(canonical_path);
        const wz::asset::Hash bytes_hash = detail::hash_bytes(bytes);
        return wz::asset::AssetKey{
            .content_hash = {
                detail::mix64(path_hash.lo, bytes_hash.lo),
                detail::mix64(path_hash.hi, bytes_hash.hi),
            },
            .schema_hash = detail::hash_u64(schema.value),
            .compiler_hash = {},   // carriers perform no transformation
            .deps_hash = {},   // file nodes have no DAG prerequisites
        };
    }
}
