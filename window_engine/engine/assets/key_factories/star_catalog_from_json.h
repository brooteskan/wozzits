#pragma once

// engine/assets/key_factories/star_catalog_from_json.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

namespace wz::engine::assets
{
    // Key for a star catalog compiled from a baked JSON document. The whole
    // content (rows + grade dials) lives in the JSON document dependency, so
    // identity is fully determined by the JSON document key + the compiler
    // version (content_hash stays empty). Mirrors make_sky_gaussian_from_json_key.
    //
    // Note: file keys in this asset system are PATH-based, not byte-based, so
    // editing the .star_catalog.json without changing its path does NOT
    // automatically invalidate the cache -- toolhosts must invalidate externally
    // for hot-reload workflows.
    [[nodiscard]] inline wz::asset::AssetKey make_star_catalog_from_json_key(
        const wz::asset::AssetKey& json_document_key) noexcept
    {
        return wz::asset::AssetKey{
            .content_hash  = wz::asset::Hash{},
            .schema_hash   = detail::hash_u64(kStarCatalogFromJSONSchema.value),
            .compiler_hash = detail::hash_u64(
                kStarCatalogFromJSONCompilerVersion),
            .deps_hash     = detail::key_to_dep_hash(json_document_key),
        };
    }
}
