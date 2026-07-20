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
    struct AtmosphereData;
    struct EnvironmentData;

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

    // Re-point each scene node's authored atmosphere graph-node id at the
    // resolved Atmosphere AssetKey for that node in `draft` — the atmosphere
    // analogue of bridge_scene_collision_keys. Run on every (re)bind so a graph
    // swap's new keys follow the authored intent. Only touches nodes whose
    // Atmosphere component carries a non-zero atmosphere_asset_node_id; a
    // component holding only a pre-resolved key is left untouched. Clears the key
    // first so a removed/renamed authored atmosphere stops resolving the previous
    // graph's key — the frame then has no fog rather than stale fog. Returns the
    // number of nodes bridged to a live Atmosphere key.
    uint32_t bridge_scene_atmosphere_keys(
        std::span<SceneNodeAsset> nodes,
        const wz::asset::AssetGraphDraft& draft);

    // The frame's environment analogue of bridge_scene_atmosphere_keys: re-point
    // each node's Environment component at the freshly committed FrameEnvironment
    // key its environment_asset_node_id names, clearing first so a removed/renamed
    // reference stops resolving the previous graph's key.
    uint32_t bridge_scene_environment_keys(
        std::span<SceneNodeAsset> nodes,
        const wz::asset::AssetGraphDraft& draft);

    // The frame's global fog: which authored node supplies it, and the resolved
    // data a renderer's VIEW-frequency constants are filled from (6c51cf5 /
    // da6c952). The read half of the atmosphere binding, downstream of
    // bridge_scene_atmosphere_keys — the bridge resolves node id -> key, this
    // resolves key -> data.
    struct SceneFrameAtmosphere
    {
        // Null whenever the frame has no fog. A scene that never authored an
        // atmosphere is therefore unaffected by this seam.
        const AtmosphereData* atmosphere = nullptr;
        // The node `atmosphere` came from, or null when nothing was selected.
        const SceneNodeAsset* source = nullptr;
        // A SECOND enabled atmosphere, if the scene authors one. Reported rather
        // than warned about here: this runs per frame, so the warning policy
        // (rate limiting, log owner) belongs to the caller.
        const SceneNodeAsset* duplicate = nullptr;
    };

    // Select the first node carrying an ENABLED Atmosphere component and resolve
    // its bridged key through `assets`. Atmosphere is frame-global state, so a
    // second enabled one is an authoring error rather than a blend: the first
    // wins and the second comes back in `duplicate`. Disabled components are
    // skipped outright, so switching one off is how an author stages a second
    // without tripping the error.
    //
    // Returns a null `atmosphere` when no node authors one, when the selected one
    // is disabled, when its key has not been bridged yet, or when the key does
    // not resolve — an unresolved key is transient by design (it is empty between
    // a graph edit and the (re)bind that re-bridges it), so it is silent here
    // rather than an error.
    //
    // The returned pointer is INTO the library's AtmosphereTable and is valid
    // until the library resolves again. Callers re-resolve per frame instead of
    // caching, which is what lets an edited Atmosphere asset take effect without
    // rebuilding the scene.
    [[nodiscard]] SceneFrameAtmosphere resolve_scene_frame_atmosphere(
        std::span<const SceneNodeAsset> nodes,
        const EngineAssetLibrary& assets);

    // The frame's global environment: which authored node supplies it, and the
    // resolved bundle a renderer reads its pieces from. The read half of the
    // Environment binding, downstream of bridge_scene_environment_keys (node id
    // -> key; this resolves key -> data). Successor to SceneFrameAtmosphere; the
    // app prefers this and falls back to that.
    struct SceneFrameEnvironment
    {
        // Null whenever the frame authors no (enabled, resolved) environment.
        const EnvironmentData* environment = nullptr;
        // The node `environment` came from, or null when nothing was selected.
        const SceneNodeAsset* source = nullptr;
        // A SECOND enabled environment, if the scene authors one -- reported, not
        // warned about here (this runs per frame; the warning policy is the
        // caller's), exactly like SceneFrameAtmosphere::duplicate.
        const SceneNodeAsset* duplicate = nullptr;
    };

    // Select the first node carrying an ENABLED Environment component and resolve
    // its bridged key through `assets`. Frame-global, so a second enabled one is
    // an authoring error rather than a blend: the first wins, the second comes
    // back in `duplicate`. Null `environment` when none is authored, the selected
    // one is disabled, or its key has not bridged / does not resolve (transient
    // mid-edit, silent by design). The pointer is INTO the library's
    // EnvironmentTable, valid until the next resolve; callers re-resolve per frame.
    [[nodiscard]] SceneFrameEnvironment resolve_scene_frame_environment(
        std::span<const SceneNodeAsset> nodes,
        const EngineAssetLibrary& assets);

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
