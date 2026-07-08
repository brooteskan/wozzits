#pragma once

// engine/assets/key_factories/star_catalog_from_ply.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>
#include <engine/starfield/star_catalog.h>

#include <cstdint>
#include <cstring>

namespace wz::engine::assets
{
    // Key for a star catalog compiled from a baked PLY point cloud + the import
    // dials. The PLY bytes live in the file dependency, but the creative dials
    // are authored node params (not in the dep), so they fold into content_hash
    // -- re-tuning a dial re-imports. Deps carry the raw-file key.
    [[nodiscard]] inline wz::asset::AssetKey make_star_catalog_from_ply_key(
        const wz::asset::AssetKey& source_file_key,
        const wz::engine::starfield::StarImportParams& params) noexcept
    {
        uint64_t h = kStarCatalogFromPLYSchema.value;
        const auto mix_double = [&h](double value) {
            uint64_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            h = detail::mix64(h, bits);
        };
        mix_double(params.reference_magnitude);
        mix_double(params.exposure);
        mix_double(params.color_saturation);
        mix_double(params.magnitude_min);
        mix_double(params.magnitude_max);
        mix_double(params.magnitude_pivot);
        mix_double(params.magnitude_contrast);
        mix_double(params.warp_amplitude);
        mix_double(params.warp_frequency);
        mix_double(params.solid_angle);

        return wz::asset::AssetKey{
            .content_hash  = detail::hash_u64(h),
            .schema_hash   = detail::hash_u64(kStarCatalogFromPLYSchema.value),
            .compiler_hash = detail::hash_u64(
                kStarCatalogFromPLYCompilerVersion),
            .deps_hash     = detail::key_to_dep_hash(source_file_key),
        };
    }
}
