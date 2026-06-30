#pragma once

#include <engine/assets/scene/scene_asset_data.h>

#include <optional>
#include <span>
#include <string_view>

namespace wz::engine::assets
{
    // Carve a node subtree out of a scene as a standalone, self-contained scene
    // (the first milestone of the prefab system): the node `root_id` plus all of
    // its transitive descendants become the nodes of a fresh SceneAssetData, with
    // the root re-rooted to the origin so a future spawn places the prefab purely
    // by the spawn transform.
    //
    // A node is included iff it is `root_id` itself, or reaches `root_id` by
    // walking its parent_id chain within `nodes` (so unrelated siblings, the
    // root's own parent, and disconnected nodes are excluded). The exported root
    // node has its parent_id cleared (the "no parent" representation: parent_id
    // is std::optional and a root carries std::nullopt) and its local transform
    // normalized to identity; descendants keep their own local TRS (correct
    // relative to the now-identity root) and their parent_id's (which all point
    // within the subtree). Returns std::nullopt if `root_id` is not found.
    //
    // Pure over the node span — no app/ABI/file dependency — so it unit-tests
    // headless. The resulting SceneAssetData carries only the nodes; the caller
    // serializes it through export_scene_to_json_document.
    std::optional<SceneAssetData> extract_scene_subtree(
        std::span<const SceneNodeAsset> nodes,
        std::string_view root_id);

} // namespace wz::engine::assets
