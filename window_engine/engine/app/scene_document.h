#pragma once

// engine/app/scene_document.h
//
// SceneDocument — the authored, serialized scene document (#258 avenue 2, the
// #183 "editing model into the engine" extraction). It owns the node array and
// the edit-time bookkeeping that the #221 ownership contract names the single
// source of truth for the scene's NON-transform authoring data (the single
// writer is the edit verbs). Transform + hierarchy live truth is NOT here — it
// belongs to the simulation polytree; reads go through the host's
// scene_world_transforms / node_world_transform, and the one transform edit seam
// is the host's polytree write.
//
// This is the first extraction stage: the document owns the STORAGE, exposed to
// the host (WozzitsApp_v1) that still holds the edit verbs + the reactions.
// Later stages move the pure mutators (each returning a SceneChange) and the pure
// readers onto it, leaving the host as the composition root that maps each
// SceneChange to its reaction (apply_scene_change) — reactions touch the behavior
// runtime / renderer / bound graph and stay with the host.

#include <engine/app/scene_change.h>                 // SceneChange, SceneEdit
#include <engine/assets/scene/scene_asset_data.h>    // SceneNodeAsset
#include <asset/draft.h>                             // AssetGraphDraftNodeId
#include <scene/scene_ecs.h>                          // AuthoredEntityId

#include <optional>
#include <string>
#include <vector>

namespace wz::app
{
    class SceneDocument
    {
    public:
        // The authored node array — the serialized document. Mutable access for
        // the edit verbs and the reactions that re-bridge keys / graft children.
        std::vector<wz::engine::assets::SceneNodeAsset>& nodes() { return nodes_; }
        const std::vector<wz::engine::assets::SceneNodeAsset>& nodes() const
        {
            return nodes_;
        }

        // Ids of nodes currently grafted from a scene_source reference (#213
        // instance mode): runtime-only children appended to nodes(), tracked so a
        // re-graft drops the previous graft and save excludes them.
        std::vector<wz::scene::AuthoredEntityId>& grafted_ids() { return grafted_ids_; }
        const std::vector<wz::scene::AuthoredEntityId>& grafted_ids() const
        {
            return grafted_ids_;
        }

        // Unsaved-edits flag: every mutating verb sets it, save clears it. Exposed
        // by reference so the existing `dirty() = dirty() || ok` assignments keep
        // working; a later stage narrows this to mark/clear.
        bool& dirty() { return dirty_; }
        bool  dirty() const { return dirty_; }

        // --- pure-document queries (read nodes()/grafted_ids() only) ------------
        // The read side of the editing model: any host or tool holding a document
        // can query it directly. WozzitsApp_v1's node_* accessors delegate here.
        // Transform-derived reads (node_local_translation / world transforms) are
        // NOT here — they cross into the simulation polytree and stay with the host.
        bool node_has_component(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& kind) const;
        std::size_t child_node_count(
            const wz::scene::AuthoredEntityId& parent_id) const;
        std::optional<wz::asset::AssetGraphDraftNodeId> node_renderable_asset_node_id(
            const wz::scene::AuthoredEntityId& node_id) const;
        std::optional<wz::asset::AssetGraphDraftNodeId> node_scene_source_node_id(
            const wz::scene::AuthoredEntityId& node_id) const;
        bool node_has_glb_scene_source(
            const wz::scene::AuthoredEntityId& node_id) const;
        const wz::engine::assets::SceneGLBSceneSource* node_glb_scene_source(
            const wz::scene::AuthoredEntityId& node_id) const;
        const wz::engine::assets::SceneCollisionAsset* node_collision(
            const wz::scene::AuthoredEntityId& node_id) const;
        const wz::engine::assets::SceneMotionAsset* node_motion(
            const wz::scene::AuthoredEntityId& node_id) const;
        // A snapshot copy of the grafted (scene-source instance) children.
        std::vector<wz::engine::assets::SceneNodeAsset> grafted_nodes() const;

        // --- structural mutators (mutate nodes()/dirty(), return a SceneEdit) ---
        // Each performs the pure document mutation and returns the mutation result
        // plus the SceneChange the host dispatches. No runtime/renderer contact —
        // the reaction (rebuild etc.) is the host's, via apply_scene_change.
        SceneEdit<wz::engine::assets::SceneAddChildResult> add_child(
            const wz::scene::AuthoredEntityId& parent_id);
        SceneEdit<bool> set_properties(
            const wz::scene::AuthoredEntityId& id,
            std::string name,
            bool visible);
        SceneEdit<bool> reparent(
            const wz::scene::AuthoredEntityId& id,
            const wz::scene::AuthoredEntityId& new_parent_id);
        SceneEdit<bool> remove(const wz::scene::AuthoredEntityId& id);
        SceneEdit<bool> reorder(
            const wz::scene::AuthoredEntityId& id,
            const wz::scene::AuthoredEntityId& before_id);
        SceneEdit<bool> set_render_order(
            const wz::scene::AuthoredEntityId& id,
            int render_order);

    private:
        std::vector<wz::engine::assets::SceneNodeAsset> nodes_{};
        std::vector<wz::scene::AuthoredEntityId>        grafted_ids_{};
        bool                                            dirty_ = false;
    };
}
