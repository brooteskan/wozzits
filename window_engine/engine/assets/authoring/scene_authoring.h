#pragma once

// engine/assets/authoring/scene_authoring.h
//
// UI-agnostic editing operations over an authored SceneAssetData.
//
// Moved out of the imgui scene editor (#183). The editor shell used to mutate
// SceneNodeAsset fields directly from its panels (gizmo transform writes, rename,
// component field edits, renderable binding, node add/delete/reparent). Those are
// scene-model mutations, not GUI concerns: the shell now collects input and calls
// these verbs, and never writes node fields itself.
//
// These build on the attach_*/detach_* component primitives in scene_asset_data.h
// and preserve the editor's existing behavior — notably remove_scene_node clears
// the active-camera default and terrain cross-references into the removed subtree,
// and reparent_scene_node rejects self-parenting and cycles. Scene-editor policy
// that is not a model invariant (e.g. the special "root" node, selection, the
// dirty flag, scene rebuild) stays in the shell. No imgui here.

#include <engine/assets/scene/scene_asset_data.h>

#include <asset/draft.h>   // AssetGraphDraftNodeId
#include <asset/types.h>   // AssetKey
#include <scene/scene_ecs.h>  // AuthoredEntityId, TransformNode::MotionType

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace wz::engine::assets::authoring
{
    using SceneNodeId = wz::scene::AuthoredEntityId;

    // ── queries ──────────────────────────────────────────────────────────────
    //
    // Node lookup reuses wz::engine::assets::find_scene_node (scene_asset_data.h),
    // which is visible to these verbs via the enclosing namespace.

    [[nodiscard]] bool scene_node_exists(
        const SceneAssetData& scene,
        const SceneNodeId& id);

    // True when `node` is a strict descendant of `ancestor` (walks parent links;
    // node == ancestor is false).
    [[nodiscard]] bool is_scene_node_descendant_of(
        const SceneAssetData& scene,
        const SceneNodeId& node,
        const SceneNodeId& ancestor);

    // Append `root` and all of its descendants to `out` (de-duplicated).
    void collect_scene_subtree_ids(
        const SceneAssetData& scene,
        const SceneNodeId& root,
        std::vector<SceneNodeId>& out);

    // ── node lifecycle ─────────────────────────────────────────────────────────

    // Insert a node with the given id (must be non-empty and unique) under
    // parent_id (nullopt => no parent). visible defaults to true. Returns false
    // if the id is empty/duplicate, or if parent_id is given but equals id or
    // names a node that does not exist.
    bool add_scene_node(
        SceneAssetData& scene,
        SceneNodeId id,
        std::optional<SceneNodeId> parent_id,
        std::string name = {});

    // Remove the node and its descendants. Resets defaults.active_camera_node and
    // clears terrain cross-references (render-style light/ambient/environment and
    // terrain mesh / height-field source_node) that pointed into the removed set.
    // Returns the removed ids; empty if the node did not exist.
    std::vector<SceneNodeId> remove_scene_node(
        SceneAssetData& scene,
        const SceneNodeId& id);

    // Set the node's parent (nullopt => detach to top level). Rejects a missing
    // node, a missing/self new parent, or a new parent that is a descendant of
    // the node (cycle). Returns false on rejection.
    bool reparent_scene_node(
        SceneAssetData& scene,
        const SceneNodeId& id,
        std::optional<SceneNodeId> new_parent_id);

    // Set the node's display name. Returns false if the node is missing.
    bool rename_scene_node(
        SceneAssetData& scene,
        const SceneNodeId& id,
        std::string name);

    // ── transform + flags ───────────────────────────────────────────────────────

    bool set_node_transform(
        SceneAssetData& scene,
        const SceneNodeId& id,
        const AuthoredTransform& transform);
    bool set_node_translation(
        SceneAssetData& scene,
        const SceneNodeId& id,
        std::array<float, 3> translation);
    bool set_node_rotation_quat(
        SceneAssetData& scene,
        const SceneNodeId& id,
        std::array<float, 4> rotation_quat);
    bool set_node_scale(
        SceneAssetData& scene,
        const SceneNodeId& id,
        std::array<float, 3> scale);
    bool set_node_visible(
        SceneAssetData& scene,
        const SceneNodeId& id,
        bool visible);
    bool set_node_motion_type(
        SceneAssetData& scene,
        const SceneNodeId& id,
        wz::scene::TransformNode::MotionType motion_type);

    // ── renderable binding (authored asset-graph node ref) ───────────────────────

    // Bind the node's renderable to an authored graph node (+ optional resolved
    // key). Rejects INVALID_ASSET_GRAPH_DRAFT_NODE (use clear_node_renderable to
    // unbind) and leaves any existing binding unchanged on rejection.
    bool assign_node_renderable(
        SceneAssetData& scene,
        const SceneNodeId& id,
        wz::asset::AssetGraphDraftNodeId graph_node,
        wz::asset::AssetKey resolved = {});
    bool clear_node_renderable(
        SceneAssetData& scene,
        const SceneNodeId& id);

    // ── generic component set / remove (plain optional<C> node members) ──────────

    // Set an optional component member to `value`. Returns false if the node is
    // missing. Components with extra invariants (renderable binding, behaviors)
    // have dedicated verbs above / below; this covers the plain optional<C> ones.
    template<class C>
    bool set_node_component(
        SceneAssetData& scene,
        const SceneNodeId& id,
        std::optional<C> SceneNodeAsset::* member,
        C value)
    {
        SceneNodeAsset* node = find_scene_node(scene, id);
        if (!node) {
            return false;
        }
        (node->*member) = std::move(value);
        return true;
    }

    // Clear an optional component member. Returns false if the node is missing.
    template<class C>
    bool remove_node_component(
        SceneAssetData& scene,
        const SceneNodeId& id,
        std::optional<C> SceneNodeAsset::* member)
    {
        SceneNodeAsset* node = find_scene_node(scene, id);
        if (!node) {
            return false;
        }
        (node->*member).reset();
        return true;
    }
}
