#pragma once

#include <engine/assets/scene/scene_asset_data.h>
#include <file/filesystem.h>

#include <string_view>

namespace wz::engine::assets
{
    // The id given to a fresh scenelet's single root node.
    inline constexpr std::string_view kSceneletRootNodeId = "root";

    // Build a minimal, VALID scenelet document: exactly one root node named
    // `name` — identity transform, visible + active, no renderable and no
    // behaviours. This is the starting point the editor then fleshes out in the
    // viewport, and it is the engine's answer to "mint me a scene": the scene
    // schema has exactly one author (export_scene_to_json_document), so a schema
    // change cannot leave a second, hand-written copy behind (issue #271).
    //
    // Pure — no file or ABI dependency — so it unit-tests headless. Serialize it
    // through export_scene_to_json_document.
    SceneAssetData make_minimal_scenelet(std::string_view name);

    // Is `name` usable as a scenelet file name? Rejects empty/blank names and
    // any name carrying a path separator, a drive colon, or a ".." component,
    // so a name that arrived from the editor cannot escape the scenelets folder.
    bool is_valid_scenelet_name(std::string_view name);

    // The project's scenelets folder, RESOURCE-RELATIVE, derived from the scene
    // file's own directory. Single owner of the "scenelets live in a folder next
    // to the scene file" convention — register_scenelet_prefabs resolves this
    // same relative path through the asset file system to build its catalog.
    wz::fs::Path scenelets_folder_for_scene(const wz::fs::Path& scene_path);

    // Resource-relative path of the scenelet file for `name`, i.e.
    // "<scenelets folder>/<name>.scene.json" — the same form the scenelet
    // catalog reports and wz_host_runtime_open_scene accepts, so a caller never
    // has to build a scene path itself.
    wz::fs::Path scenelet_relative_path(
        const wz::fs::Path& scene_path,
        std::string_view name);

} // namespace wz::engine::assets
