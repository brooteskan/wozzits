#pragma once

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

namespace wz::engine::assets
{
    [[nodiscard]] inline wz::asset::AssetKey make_toml_document_key(
        const wz::asset::AssetKey& source_file_key) noexcept
    {
        return wz::asset::AssetKey{
            .content_hash = { 0, 0 },
            .schema_hash = detail::hash_u64(kTOMLDocumentSchema.value),
            .compiler_hash = detail::hash_u64(kTOMLDocumentCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(source_file_key),
        };
    }

} // namespace wz::engine::assets