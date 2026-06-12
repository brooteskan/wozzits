#pragma once

// engine/assets/key_factories/mesh_render_style.h

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/mesh_render_style/mesh_render_style.h>
#include <engine/assets/schema_ids.h>

#include <cstring>
#include <string_view>

namespace wz::engine::assets
{
    [[nodiscard]] inline uint64_t mesh_render_style_float_bits(
        float value) noexcept
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return static_cast<uint64_t>(bits);
    }

    [[nodiscard]] inline wz::asset::AssetKey make_mesh_render_style_key(
        std::string_view name,
        const MeshRenderStyleData& style) noexcept
    {
        uint64_t h = detail::fnv1a_64(name);
        const auto mix_layer =
            [&h](const MeshRenderLayerStyle& layer) noexcept {
                h = detail::mix64(h, layer.enabled ? 1ull : 0ull);
                for (float channel : layer.color) {
                    h = detail::mix64(
                        h,
                        mesh_render_style_float_bits(channel));
                }
                h = detail::mix64(
                    h,
                    mesh_render_style_float_bits(layer.emissive_strength));
        };
        mix_layer(style.wireframe);
        mix_layer(style.surface);
        h = detail::mix64(h, mesh_render_style_float_bits(style.alpha));
        h = detail::mix64(h, style.depth_test ? 1ull : 0ull);
        h = detail::mix64(h, style.depth_write ? 1ull : 0ull);
        h = detail::mix64(h, style.double_sided ? 1ull : 0ull);
        h = detail::mix64(h, style.hidden_line_prepass ? 1ull : 0ull);
        h = detail::mix64(
            h,
            style.field_visualization.enabled ? 1ull : 0ull);
        h = detail::mix64(
            h,
            static_cast<uint64_t>(style.field_visualization.channel_id));
        h = detail::mix64(
            h,
            mesh_render_style_float_bits(
                style.field_visualization.value_min));
        h = detail::mix64(
            h,
            mesh_render_style_float_bits(
                style.field_visualization.value_max));
        h = detail::mix64(
            h,
            mesh_render_style_float_bits(style.field_visualization.gamma));
        h = detail::mix64(
            h,
            static_cast<uint64_t>(style.field_visualization.palette));
        h = detail::mix64(h, style.mask.enabled ? 1ull : 0ull);
        h = detail::mix64(h, static_cast<uint64_t>(style.mask.domain));
        h = detail::mix64(
            h,
            static_cast<uint64_t>(style.mask.projection_mode));
        h = detail::mix64(
            h,
            static_cast<uint64_t>(style.mask.overlap_mode));
        for (float channel : style.mask.unmatched_color) {
            h = detail::mix64(h, mesh_render_style_float_bits(channel));
        }
        h = detail::mix64(h, style.mask.show_unmatched ? 1ull : 0ull);
        h = detail::mix64(
            h,
            static_cast<uint64_t>(style.mask.rules.size()));
        for (const MeshMaskRule& rule : style.mask.rules) {
            h = detail::mix64(h, rule.enabled ? 1ull : 0ull);
            h = detail::mix64(h, rule.input_channel_id);
            h = detail::mix64(h, mesh_render_style_float_bits(rule.lo));
            h = detail::mix64(h, mesh_render_style_float_bits(rule.hi));
            for (float channel : rule.color) {
                h = detail::mix64(h, mesh_render_style_float_bits(channel));
            }
            h = detail::mix64(
                h,
                static_cast<uint64_t>(
                    static_cast<int64_t>(rule.priority)));
        }

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(kMeshRenderStyleSchema.value),
            .compiler_hash = detail::hash_u64(kMeshRenderStyleCompilerVersion),
            .deps_hash = {},
        };
    }
}
