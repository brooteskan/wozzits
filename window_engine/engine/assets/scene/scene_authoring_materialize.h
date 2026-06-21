#pragma once

#include <asset/types.h>
#include <engine/assets/scene/scene_asset_data.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace wz::asset
{
    struct AssetGraphDraft;
}

namespace wz::engine::assets
{
    class EngineAssetLibrary;

    struct SceneAuthoringMaterializeOptions
    {
        bool create_preview_renderables = true;
        bool create_terrain_surface_renderables = true;
        bool create_terrain_debug_renderables = true;
    };

    struct SceneAuthoringMaterializeReport
    {
        bool ok = false;
        std::string error;
        std::vector<wz::asset::AssetKey> renderables_to_realize;
    };

    SceneAuthoringMaterializeReport materialize_scene_authoring_components(
        SceneAssetData& scene,
        EngineAssetLibrary& assets,
        const SceneAuthoringMaterializeOptions& options = {});

    SceneAssetData make_default_scene_authoring_scene(
        std::string name = "scene_editor_scene");

    // Re-point each scene node's authored renderable graph-node id at the
    // resolved AssetKey for that node in `draft` — the runtime side of the
    // authored-vs-resolved identity rule. Run on every (re)bind: a graph swap
    // mints new keys, so a node's renderable_asset must follow or it draws
    // nothing/stale. Clears the key first, so a removed/renamed authored
    // renderable stops drawing the previous graph's (still-resolvable) key.
    // Returns the number of nodes bridged to a live key.
    uint32_t bridge_scene_renderable_keys(
        std::span<SceneNodeAsset> nodes,
        const wz::asset::AssetGraphDraft& draft);

} // namespace wz::engine::assets
