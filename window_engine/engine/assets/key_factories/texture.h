#pragma once

// engine/assets/key_factories/texture.h
//
// Key for a texture imported from an image file. The pixels live in the file
// dependency (folded into deps_hash); the semantic dials -- usage and colour
// space -- are authored node params (not in the dep bytes), so they fold into
// content_hash: changing the usage or colour space re-imports. Mirror of
// make_star_catalog_from_ply_key (a raw-file dep + authored dials).

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>

#include <cstdint>

namespace wz::engine::assets
{
    [[nodiscard]] inline wz::asset::AssetKey make_texture_from_file_key(
        const wz::asset::AssetKey& source_file_key,
        int usage_index,
        int color_space_index) noexcept
    {
        uint64_t h = kTextureFromFileSchema.value;
        h = detail::mix64(h, static_cast<uint64_t>(usage_index));
        h = detail::mix64(h, static_cast<uint64_t>(color_space_index));

        return wz::asset::AssetKey{
            .content_hash  = detail::hash_u64(h),
            .schema_hash   = detail::hash_u64(kTextureFromFileSchema.value),
            .compiler_hash = detail::hash_u64(kTextureCompilerVersion),
            .deps_hash     = detail::key_to_dep_hash(source_file_key),
        };
    }
}
