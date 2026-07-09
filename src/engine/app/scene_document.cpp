// src/engine/app/scene_document.cpp

#include <engine/app/scene_document.h>

#include <string>
#include <unordered_set>
#include <utility>

namespace wz::app
{
    bool SceneDocument::node_has_component(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& kind) const
    {
        return wz::engine::assets::node_has_optional_component(
            nodes_, node_id, kind);
    }

    std::size_t SceneDocument::child_node_count(
        const wz::scene::AuthoredEntityId& parent_id) const
    {
        std::size_t count = 0;
        for (const wz::engine::assets::SceneNodeAsset& node : nodes_) {
            if (node.parent_id && *node.parent_id == parent_id) {
                ++count;
            }
        }
        return count;
    }

    std::optional<wz::asset::AssetGraphDraftNodeId>
    SceneDocument::node_renderable_asset_node_id(
        const wz::scene::AuthoredEntityId& node_id) const
    {
        const wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(nodes_, node_id);
        if (!node) {
            return std::nullopt;
        }
        return node->renderable_asset_node_id;
    }

    std::optional<wz::asset::AssetGraphDraftNodeId>
    SceneDocument::node_scene_source_node_id(
        const wz::scene::AuthoredEntityId& node_id) const
    {
        const wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(nodes_, node_id);
        if (!node) {
            return std::nullopt;
        }
        return node->scene_source_node_id;
    }

    bool SceneDocument::node_has_glb_scene_source(
        const wz::scene::AuthoredEntityId& node_id) const
    {
        const wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(nodes_, node_id);
        return node && node->glb_scene_source.has_value();
    }

    const wz::engine::assets::SceneGLBSceneSource*
    SceneDocument::node_glb_scene_source(
        const wz::scene::AuthoredEntityId& node_id) const
    {
        const wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(nodes_, node_id);
        if (!node || !node->glb_scene_source) {
            return nullptr;
        }
        return &*node->glb_scene_source;
    }

    const wz::engine::assets::SceneCollisionAsset* SceneDocument::node_collision(
        const wz::scene::AuthoredEntityId& node_id) const
    {
        const wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(nodes_, node_id);
        if (!node || !node->collision) {
            return nullptr;
        }
        return &*node->collision;
    }

    const wz::engine::assets::SceneMotionAsset* SceneDocument::node_motion(
        const wz::scene::AuthoredEntityId& node_id) const
    {
        const wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(nodes_, node_id);
        if (!node || !node->motion) {
            return nullptr;
        }
        return &*node->motion;
    }

    std::vector<wz::engine::assets::SceneNodeAsset>
    SceneDocument::grafted_nodes() const
    {
        if (grafted_ids_.empty()) {
            return {};
        }

        const std::unordered_set<std::string> grafted(
            grafted_ids_.begin(), grafted_ids_.end());
        std::vector<wz::engine::assets::SceneNodeAsset> out;
        out.reserve(grafted.size());
        for (const wz::engine::assets::SceneNodeAsset& node : nodes_) {
            if (grafted.count(node.id) != 0) {
                out.push_back(node);  // copy: the seam hands a snapshot back
            }
        }
        return out;
    }

    // --- structural mutators -------------------------------------------------

    SceneEdit<wz::engine::assets::SceneAddChildResult> SceneDocument::add_child(
        const wz::scene::AuthoredEntityId& parent_id)
    {
        wz::engine::assets::SceneAddChildResult result =
            wz::engine::assets::add_child_scene_node(nodes_, parent_id);
        SceneChange change = SceneChange::none();
        if (result.ok) {
            // Re-bake so the new child flattens into pre-order right after its
            // parent's subtree (draw order = tree order).
            wz::engine::assets::bake_scene_node_draw_order(nodes_);
            dirty_ = true;
            change = SceneChange::structural();
        }
        return { std::move(result), change };
    }

    SceneEdit<bool> SceneDocument::set_properties(
        const wz::scene::AuthoredEntityId& id,
        std::string name,
        bool visible)
    {
        const bool ok = wz::engine::assets::set_scene_node_properties(
            nodes_, id, std::move(name), visible);
        dirty_ = dirty_ || ok;
        // Pure document edit: the renderer reads name/visible fresh next frame.
        return { ok, SceneChange::none() };
    }

    SceneEdit<bool> SceneDocument::reparent(
        const wz::scene::AuthoredEntityId& id,
        const wz::scene::AuthoredEntityId& new_parent_id)
    {
        const bool ok = wz::engine::assets::reparent_scene_node(
            nodes_, id, new_parent_id);
        SceneChange change = SceneChange::none();
        if (ok) {
            // Nesting drives draw order (pre-order); a reparent re-bakes so the
            // moved subtree flattens under its new parent by its array slot.
            wz::engine::assets::bake_scene_node_draw_order(nodes_);
            dirty_ = true;
            change = SceneChange::structural();
        }
        return { ok, change };
    }

    SceneEdit<bool> SceneDocument::remove(const wz::scene::AuthoredEntityId& id)
    {
        const bool removed =
            !wz::engine::assets::remove_scene_node(nodes_, id).empty();
        dirty_ = dirty_ || removed;
        return { removed,
                 removed ? SceneChange::structural() : SceneChange::none() };
    }

    SceneEdit<bool> SceneDocument::reorder(
        const wz::scene::AuthoredEntityId& id,
        const wz::scene::AuthoredEntityId& before_id)
    {
        const bool moved = wz::engine::assets::reorder_scene_node(
            nodes_, id, before_id);
        SceneChange change = SceneChange::none();
        if (moved) {
            // The reorder set this node's sibling slot; re-bake keeps tree
            // pre-order with render_order as the dominant layer.
            wz::engine::assets::bake_scene_node_draw_order(nodes_);
            dirty_ = true;
            change = SceneChange::structural();
        }
        return { moved, change };
    }

    SceneEdit<bool> SceneDocument::set_render_order(
        const wz::scene::AuthoredEntityId& id,
        int render_order)
    {
        const bool changed = wz::engine::assets::set_scene_node_render_order(
            nodes_, id, render_order);
        SceneChange change = SceneChange::none();
        if (changed) {
            // Layer changed: re-bake so the node moves into its render_order layer.
            wz::engine::assets::bake_scene_node_draw_order(nodes_);
            dirty_ = true;
            change = SceneChange::structural();
        }
        return { changed, change };
    }
}
