#pragma once

// engine/assets/key_factories/hlsl_shader.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

#include <span>
#include <string_view>

namespace wz::engine::assets {

    // Key for an HLSL shader node.
    // The key encodes the entry point, target profile, and shader stage so that
    // two shaders compiled from the same file with different parameters are
    // treated as distinct assets. The source file key feeds into deps_hash so that
    // a path change upstream invalidates this node too.
    //
    // A shader may declare SEVERAL sources -- that is how shared HLSL is shared
    // here (#289): the common BRDF is wired ahead of the entry body and the two
    // are concatenated. EVERY source must therefore fold into deps_hash. Folding
    // only the first would give two variants that share a common source and
    // agree on stage/entry/target the SAME key, and one program would silently
    // be used for both.
    [[nodiscard]] inline wz::asset::AssetKey make_hlsl_shader_key(
        std::span<const wz::asset::AssetKey> source_file_keys,
        uint8_t stage_ordinal,
        std::string_view entry,
        std::string_view target) noexcept
    {
        const wz::asset::Hash entry_h = detail::hash_str(entry);
        const wz::asset::Hash target_h = detail::hash_str(target);
        const wz::asset::Hash stage_h = detail::hash_u64(stage_ordinal);

        const wz::asset::Hash content = {
            detail::mix64(detail::mix64(entry_h.lo, target_h.lo), stage_h.lo),
            detail::mix64(detail::mix64(entry_h.hi, target_h.hi), stage_h.hi),
        };

        // Order matters: [common, entry] is a different shader from
        // [entry, common], because concatenation order is the contract.
        wz::asset::Hash deps{};
        for (const wz::asset::AssetKey& key : source_file_keys) {
            deps = detail::combine_dep_hashes(deps, detail::key_to_dep_hash(key));
        }

        return wz::asset::AssetKey{
            .content_hash = content,
            .schema_hash = detail::hash_u64(kHLSLShaderSchema.value),
            .compiler_hash = detail::hash_u64(kHLSLCompilerVersion),
            .deps_hash = deps,
        };
    }

    // Single-source convenience: the shape most shader nodes have.
    [[nodiscard]] inline wz::asset::AssetKey make_hlsl_shader_key(
        const wz::asset::AssetKey& source_file_key,
        uint8_t stage_ordinal,
        std::string_view entry,
        std::string_view target) noexcept
    {
        const wz::asset::AssetKey one[1] = { source_file_key };
        return make_hlsl_shader_key(
            std::span<const wz::asset::AssetKey>{ one, 1 },
            stage_ordinal, entry, target);
    }
}