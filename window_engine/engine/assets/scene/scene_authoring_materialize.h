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

    // Re-point each scene node's authored scene-source graph-node id at the
    // resolved Scene AssetKey for that node in `draft` — the scene-source analogue
    // of bridge_scene_renderable_keys (issue #213). Run on every (re)bind so a
    // graph swap's new keys follow the authored intent. Returns the number of
    // host nodes bridged to a live Scene key.
    uint32_t bridge_scene_source_keys(
        std::span<SceneNodeAsset> nodes,
        const wz::asset::AssetGraphDraft& draft);

    // Re-point each scene node's authored collision graph-node id at the resolved
    // collision AssetKey for that node in `draft` — the collision analogue of
    // bridge_scene_renderable_keys (issue #216/#217). Run on every (re)bind so a
    // graph swap's new keys follow the authored intent. Only touches nodes whose
    // Collision component carries collision_asset_node_id; the inline
    // height_field_source materialize path (and a pre-resolved key) are left
    // untouched when no node id is set. Clears the key first so a removed/renamed
    // authored collision stops resolving the previous graph's key. Returns the
    // number of nodes bridged to a live collision key.
    uint32_t bridge_scene_collision_keys(
        std::span<SceneNodeAsset> nodes,
        const wz::asset::AssetGraphDraft& draft);

    // Expand a referenced Scene asset (`sub_scene`) into the host's child
    // SceneNodeAssets (issue #213), shared by the runtime instance graft and the
    // author-time flatten. For each sub-scene node it produces a host child:
    //   - id     = "<host.id>/<sub_node.id>" (GLB-named, namespaced under host)
    //   - parent = a sub-scene ROOT reparents to host.id; a non-root reparents to
    //              "<host.id>/<sub_parent.id>" (sub-scene parenting preserved)
    //   - local  = the sub-scene node's local transform, UNCHANGED — the renderer
    //              composes world = host_world * local via the parent walk, so the
    //              host transform sizes/places the whole sub-tree and each child's
    //              local stays independently drivable by the behavior API
    //   - renderable_asset / renderable_asset_node_id carried through
    // Nodes whose id is empty are skipped (can't form a stable namespaced id).
    // The returned vector does NOT include the host itself.
    std::vector<SceneNodeAsset> expand_scene_source_children(
        const SceneNodeAsset& host,
        const SceneAssetData& sub_scene);

} // namespace wz::engine::assets
