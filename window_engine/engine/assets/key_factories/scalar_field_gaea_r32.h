#pragma once

// engine/assets/key_factories/scalar_field_gaea_r32.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

namespace wz::engine::assets {

    // Key for a Gaea .r32 scalar field recipe node.
    //
    // Unlike make_scalar_field_key, the dimensions are NOT part of identity: they
    // are derived from the source file's length (Gaea's square convention), so the
    // source file dependency already determines them. Identity is therefore the
    // source file plus the only authored knob, domain_kind.
    //
    // source_file_key    — key of the kRawFileSchema carrier node (the .r32)
    // domain_kind_ordinal — cast from ScalarFieldDomainKind
    [[nodiscard]] inline wz::asset::AssetKey make_scalar_field_from_gaea_r32_key(
        const wz::asset::AssetKey& source_file_key,
        uint8_t domain_kind_ordinal) noexcept
    {
        return wz::asset::AssetKey{
            .content_hash =
                detail::hash_u64(static_cast<uint64_t>(domain_kind_ordinal)),
            .schema_hash =
                detail::hash_u64(kScalarFieldFromGaeaR32Schema.value),
            .compiler_hash = detail::hash_u64(kScalarFieldCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(source_file_key),
        };
    }

} // namespace wz::engine::assets
