#pragma once

// engine/assets/key_factories/puppet.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

#include <cstdint>

namespace wz::engine::assets
{
    // Key for an Inochi2D puppet loaded from a .inp / .inx (TRNSRTS) file.
    [[nodiscard]] inline wz::asset::AssetKey make_puppet_from_file_key(
        const wz::asset::AssetKey& source_file_key) noexcept
    {
        return wz::asset::AssetKey{
            .content_hash  = detail::hash_u64(kPuppetFromFileSchema.value),
            .schema_hash   = detail::hash_u64(kPuppetFromFileSchema.value),
            .compiler_hash = detail::hash_u64(kInochiPuppetCompilerVersion),
            .deps_hash     = detail::key_to_dep_hash(source_file_key),
        };
    }
}
