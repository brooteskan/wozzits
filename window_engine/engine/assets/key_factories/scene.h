#pragma once

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

namespace wz::engine::assets
{
    [[nodiscard]] inline wz::asset::AssetKey make_scene_from_json_key(
        const wz::asset::AssetKey& json_document_key,
        wz::asset::Hash reference_bindings_hash) noexcept
    {
        return wz::asset::AssetKey{
            .content_hash = reference_bindings_hash,
            .schema_hash = detail::hash_u64(kSceneFromJSONSchema.value),
            .compiler_hash = detail::hash_u64(kSceneFromJSONCompilerVersion),
            .deps_hash = detail::key_to_dep_hash(json_document_key),
        };
    }

    [[nodiscard]] inline wz::asset::AssetKey make_scene_from_json_key(
        const wz::asset::AssetKey& json_document_key) noexcept
    {
        return make_scene_from_json_key(json_document_key, {});
    }

} // namespace wz::engine::assets
