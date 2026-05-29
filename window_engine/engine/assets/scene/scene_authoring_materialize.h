#pragma once

#include <asset/types.h>
#include <engine/assets/scene/scene_asset_data.h>

#include <string>
#include <vector>

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

} // namespace wz::engine::assets
