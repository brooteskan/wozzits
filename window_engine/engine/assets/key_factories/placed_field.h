#pragma once

// engine/assets/key_factories/placed_field.h
//
// Key factory for PlacedField combiner nodes (issue #223).
//
// A PlacedField is fully determined by its two dependencies — the field and the
// Placement frame — so identity is content-addressed from those dep keys, with
// no authored name needed (two placed fields that bind the same field to the
// same placement ARE the same asset and dedup, exactly like the
// gaussian-splat-from-scalar-field combiner).
//
// deps_hash folds the field dep, then the placement dep, in the SAME order the
// module registers the graph edges (field first, placement second).
// combine_dep_hashes is not commutative, so this ordering must match the
// registration + compiler read order. Folding the placement here is what makes a
// placement change re-key the PlacedField (and thus its consumers) while leaving
// the field's own key untouched.

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>

#include <string_view>

namespace wz::engine::assets
{
    [[nodiscard]] inline wz::asset::AssetKey make_placed_field_key(
        const wz::asset::AssetKey& field_key,
        const wz::asset::AssetKey& placement_key,
        wz::asset::AssetType field_type) noexcept
    {
        // Content salt + the field's asset type so a scalar-field-placed and a
        // (future) vector-field-placed pair never collide even if their dep
        // hashes coincide. The dep keys themselves carry the actual identity.
        uint64_t h = detail::fnv1a_64(std::string_view{ "placed_field" });
        h = detail::mix64(h, static_cast<uint64_t>(field_type));

        wz::asset::Hash deps_hash = detail::combine_dep_hashes(
            detail::key_to_dep_hash(field_key),
            detail::key_to_dep_hash(placement_key));

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(kPlacedFieldSchema.value),
            .compiler_hash = detail::hash_u64(kPlacedFieldCompilerVersion),
            .deps_hash = deps_hash,
        };
    }
}
