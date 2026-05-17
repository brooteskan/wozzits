#pragma once

// engine/assets/key_factories/gaussian_splat_file.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

#include <cstdint>

namespace wz::engine::assets
{
    // Key for a Gaussian splat cloud loaded from a PLY file.
    [[nodiscard]] inline wz::asset::AssetKey make_gaussian_splat_from_ply_key(
        const wz::asset::AssetKey& source_file_key) noexcept
    {
        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(kGaussianSplatFromPLYSchema.value),
            .schema_hash = detail::hash_u64(kGaussianSplatFromPLYSchema.value),
            .compiler_hash = detail::hash_u64(kGaussianSplatCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(source_file_key),
        };
    }
}