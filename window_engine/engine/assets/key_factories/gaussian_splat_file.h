#pragma once

// engine/assets/key_factories/gaussian_splat_file.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

namespace wz::engine::assets {

    // Key for a splat cloud loaded from a file.
    [[nodiscard]] inline wz::asset::AssetKey make_gaussian_splat_file_key(
        const wz::asset::AssetKey& source_file_key,
        uint8_t format_ordinal) noexcept
    {
        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(format_ordinal),
            .schema_hash = detail::hash_u64(kGaussianSplatPLYSchema.value),
            .compiler_hash = detail::hash_u64(kGaussianSplatCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(source_file_key),
        };
    }
}