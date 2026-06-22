#pragma once

#include <engine/assets/scene/scene_asset_data.h>

#include <external/json/json_document.h>

namespace wz::engine::assets
{
    // Generated scene export. This emits the scene fields currently understood
    // by the scene JSON compiler; it is not a source-document patcher.
    wz::json::JSONDocument export_scene_to_json_document(
        const SceneAssetData& scene);

    // Replace (or add) the "nodes" array of an existing scene document with the
    // nodes re-emitted from `nodes`, preserving every other root member (schema,
    // name, lights, defaults, ...). Used to persist live node edits back to the
    // scene file without dropping non-node data. No-op if the root isn't an
    // object.
    void set_scene_document_nodes(
        wz::json::JSONDocument& document,
        const std::vector<SceneNodeAsset>& nodes);

} // namespace wz::engine::assets
