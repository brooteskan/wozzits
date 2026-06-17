#pragma once

// engine/assets/key_factories/render_program.h

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/renderable/renderable.h>
#include <engine/assets/render_program/render_program.h>

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

    // Key for a custom (fully-authored) render program node.
    // dep[0] = vertex shader key, dep[1] = pixel shader key.
    [[nodiscard]] inline wz::asset::AssetKey make_custom_render_program_key(
        const CustomRenderProgramDesc& desc) noexcept
    {
        uint64_t lo = detail::hash_str(desc.name).lo;
        uint64_t hi = detail::hash_str(desc.name).hi;

        auto mix_enum = [&](uint64_t value)
        {
            lo = detail::mix64(lo, value);
            hi = detail::mix64(hi, detail::mix64(value, 0x9e3779b97f4a7c15ull));
        };
        auto mix_string = [&](std::string_view value)
        {
            const wz::asset::Hash h = detail::hash_str(value);
            lo = detail::mix64(lo, h.lo);
            hi = detail::mix64(hi, h.hi);
        };

        mix_enum(static_cast<uint64_t>(desc.binding_model));
        mix_enum(static_cast<uint64_t>(desc.topology));
        mix_enum(static_cast<uint64_t>(desc.default_domain));
        mix_enum(static_cast<uint64_t>(desc.default_policy_flags));
        mix_enum(static_cast<uint64_t>(desc.input_layout));
        mix_enum(static_cast<uint64_t>(desc.blend_mode));
        mix_enum(static_cast<uint64_t>(desc.depth_mode));
        mix_enum(static_cast<uint64_t>(desc.raster_mode));

        mix_enum(desc.root_constants.size());
        for (const RootConstantBinding& binding : desc.root_constants) {
            mix_enum(static_cast<uint64_t>(binding.visibility));
            mix_enum(binding.shader_register);
            mix_enum(binding.register_space);
            mix_enum(binding.value_count);
            mix_string(binding.semantic);
        }

        mix_enum(desc.descriptor_bindings.size());
        for (const DescriptorBinding& binding : desc.descriptor_bindings) {
            mix_enum(static_cast<uint64_t>(binding.kind));
            mix_enum(static_cast<uint64_t>(binding.visibility));
            mix_enum(static_cast<uint64_t>(binding.semantic));
            mix_enum(binding.shader_register);
            mix_enum(binding.register_space);
            mix_enum(binding.descriptor_count);
        }

        const wz::asset::Hash content = {
            lo,
            hi,
        };

        const wz::asset::Hash dep = detail::combine_dep_hashes(
            detail::key_to_dep_hash(desc.vertex_shader),
            detail::key_to_dep_hash(desc.pixel_shader));

        return wz::asset::AssetKey{
            .content_hash  = content,
            .schema_hash   = detail::hash_u64(kCustomRenderProgramSchema.value),
            .compiler_hash = detail::hash_u64(kCustomRenderProgramCompilerVersion),
            .deps_hash     = dep,
        };
    }
}
