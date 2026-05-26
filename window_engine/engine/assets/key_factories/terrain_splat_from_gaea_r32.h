#pragma once

// engine/assets/key_factories/terrain_splat_from_gaea_r32.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

namespace wz::engine::assets
{
    // Key for a terrain splat recipe bundling a .r32 heightmap file with a
    // JSON sidecar document.  Identity is fully determined by the .r32 file
    // key, the JSON document key, and the compiler version.
    //
    // Note: file keys in this asset system are PATH-based, not byte-based,
    // so editing the .json sidecar without changing its path does NOT
    // automatically invalidate the cache.  For hot-reload workflows the
    // toolhost must invalidate the cache externally when sidecars change.
    [[nodiscard]] inline wz::asset::AssetKey
        make_terrain_splat_from_gaea_r32_key(
            const wz::asset::AssetKey& r32_file_key,
            const wz::asset::AssetKey& sidecar_json_document_key) noexcept
    {
        const wz::asset::Hash a = detail::key_to_dep_hash(r32_file_key);
        const wz::asset::Hash b = detail::key_to_dep_hash(sidecar_json_document_key);

        return wz::asset::AssetKey{
            .content_hash  = wz::asset::Hash{},
            .schema_hash   = detail::hash_u64(
                kTerrainSplatFromGaeaR32Schema.value),
            .compiler_hash = detail::hash_u64(
                kTerrainSplatFromGaeaR32CompilerVersion),
            .deps_hash     = wz::asset::Hash{
                detail::mix64(a.lo, b.lo),
                detail::mix64(a.hi, b.hi),
            },
        };
    }
}
