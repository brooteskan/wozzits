#pragma once

// engine/assets/key_factories/hlsl_shader.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

namespace wz::engine::assets {

    // Key for an HLSL shader node.
    // The key encodes the entry point, target profile, and shader stage so that
    // two shaders compiled from the same file with different parameters are
    // treated as distinct assets. The source file key feeds into deps_hash so that
    // a path change upstream invalidates this node too.
    [[nodiscard]] inline wz::asset::AssetKey make_hlsl_shader_key(
        const wz::asset::AssetKey& source_file_key,
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

        return wz::asset::AssetKey{
            .content_hash = content,
            .schema_hash = detail::hash_u64(kHLSLShaderSchema.value),
            .compiler_hash = detail::hash_u64(kHLSLCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(source_file_key),
        };
    }
}