#pragma once

#include <engine/assets/scene/scene_asset_data.h>

#include <external/json/json_document.h>

namespace wz::engine::assets
{
    // Generated scene export. This emits the scene fields currently understood
    // by the scene JSON compiler; it is not a source-document patcher.
    wz::json::JSONDocument export_scene_to_json_document(
        const SceneAssetData& scene);

} // namespace wz::engine::assets
