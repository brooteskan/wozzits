#pragma once

// engine/assets/key_factories/render_program.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/renderable/renderable.h>

namespace wz::engine::assets
{
    // Key for a builtin render program node.
    // dep[0] = vertex shader key, dep[1] = pixel shader key.
    [[nodiscard]] inline wz::asset::AssetKey make_builtin_render_program_key(
        std::string_view name,
        BuiltinRenderProgram program,
        const wz::asset::AssetKey& vertex_shader_key,
        const wz::asset::AssetKey& pixel_shader_key) noexcept
    {
        const wz::asset::Hash name_h = detail::hash_str(name);
        const wz::asset::Hash prog_h = detail::hash_u64(static_cast<uint64_t>(program));

        const wz::asset::Hash content = {
            detail::mix64(name_h.lo, prog_h.lo),
            detail::mix64(name_h.hi, prog_h.hi),
        };

        const wz::asset::Hash dep = detail::combine_dep_hashes(
            detail::key_to_dep_hash(vertex_shader_key),
            detail::key_to_dep_hash(pixel_shader_key));

        return wz::asset::AssetKey{
            .content_hash  = content,
            .schema_hash   = detail::hash_u64(kBuiltinRenderProgramSchema.value),
            .compiler_hash = detail::hash_u64(kBuiltinRenderProgramCompilerVersion),
            .deps_hash     = dep,
        };
    }
}
