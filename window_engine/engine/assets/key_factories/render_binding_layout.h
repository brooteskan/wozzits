#pragma once

// engine/assets/key_factories/render_binding_layout.h

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/render_binding_layout/render_binding_layout.h>
#include <engine/assets/schema_ids.h>

#include <string_view>

namespace wz::engine::assets
{
    [[nodiscard]] inline wz::asset::AssetKey make_render_binding_layout_key(
        std::string_view name,
        const RenderBindingLayoutData& layout) noexcept
    {
        uint64_t h = detail::fnv1a_64(name);

        h = detail::mix64(h, detail::fnv1a_64(layout.constants_semantic));
        h = detail::mix64(
            h, static_cast<uint64_t>(layout.constants_visibility));
        h = detail::mix64(h, static_cast<uint64_t>(layout.constants_head));
        h = detail::mix64(h, static_cast<uint64_t>(layout.constants_dwords));

        h = detail::mix64(
            h, static_cast<uint64_t>(layout.constant_fields.size()));
        for (const RenderBindingConstantField& field : layout.constant_fields) {
            h = detail::mix64(h, detail::fnv1a_64(field.name));
            h = detail::mix64(h, static_cast<uint64_t>(field.type));
        }

        h = detail::mix64(h, static_cast<uint64_t>(layout.bindings.size()));
        for (const RenderBindingRow& row : layout.bindings) {
            h = detail::mix64(h, detail::fnv1a_64(row.semantic));
            h = detail::mix64(h, static_cast<uint64_t>(row.kind));
            h = detail::mix64(h, static_cast<uint64_t>(row.visibility));
        }

        h = detail::mix64(h, static_cast<uint64_t>(layout.samplers.size()));
        for (const RenderBindingSamplerRow& row : layout.samplers) {
            h = detail::mix64(h, static_cast<uint64_t>(row.kind));
            h = detail::mix64(h, static_cast<uint64_t>(row.visibility));
        }

        return wz::asset::AssetKey{
            .content_hash = detail::hash_u64(h),
            .schema_hash = detail::hash_u64(kRenderBindingLayoutSchema.value),
            .compiler_hash =
                detail::hash_u64(kRenderBindingLayoutCompilerVersion),
            .deps_hash = {},
        };
    }
}
