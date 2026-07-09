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

    // --- behavior-binding mutators -------------------------------------------
    // Each applies the matching scene_asset_data.h helper, marks dirty on success,
    // and asks for an UNCONDITIONAL rebuild (adding the first binding to a scene
    // that had no runtime must create it).

    SceneEdit<wz::engine::assets::SceneAddBehaviorResult>
    SceneDocument::add_behavior(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& module)
    {
        wz::engine::assets::SceneAddBehaviorResult result =
            wz::engine::assets::add_node_behavior(nodes_, node_id, module);
        SceneChange change = SceneChange::none();
        if (result.ok) {
            dirty_ = true;
            change = SceneChange::runtime_rebuild();
        }
        return { std::move(result), change };
    }

    SceneEdit<bool> SceneDocument::remove_behavior(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& binding_id)
    {
        const bool ok = wz::engine::assets::remove_node_behavior(
            nodes_, node_id, binding_id);
        if (ok) {
            dirty_ = true;
        }
        return { ok, ok ? SceneChange::runtime_rebuild() : SceneChange::none() };
    }

    SceneEdit<bool> SceneDocument::set_behavior_enabled(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& binding_id,
        bool enabled)
    {
        const bool ok = wz::engine::assets::set_node_behavior_enabled(
            nodes_, node_id, binding_id, enabled);
        if (ok) {
            dirty_ = true;
        }
        return { ok, ok ? SceneChange::runtime_rebuild() : SceneChange::none() };
    }

    SceneEdit<bool> SceneDocument::set_behavior_fields(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& binding_id,
        const std::string& label,
        const std::string& module)
    {
        const bool ok = wz::engine::assets::set_node_behavior_fields(
            nodes_, node_id, binding_id, label, module);
        if (ok) {
            dirty_ = true;
        }
        return { ok, ok ? SceneChange::runtime_rebuild() : SceneChange::none() };
    }

    SceneEdit<bool> SceneDocument::set_behavior_events(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& binding_id,
        const std::vector<std::string>& events)
    {
        const bool ok = wz::engine::assets::set_node_behavior_events(
            nodes_, node_id, binding_id, events);
        if (ok) {
            dirty_ = true;
        }
        return { ok, ok ? SceneChange::runtime_rebuild() : SceneChange::none() };
    }

    SceneEdit<bool> SceneDocument::set_behavior_config(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& binding_id,
        const wz::engine::assets::SceneBehaviorConfigValue& value)
    {
        const bool ok = wz::engine::assets::set_node_behavior_config(
            nodes_, node_id, binding_id, value);
        if (ok) {
            dirty_ = true;
        }
        return { ok, ok ? SceneChange::runtime_rebuild() : SceneChange::none() };
    }

    SceneEdit<bool> SceneDocument::clear_behavior_config(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& binding_id,
        const std::string& key)
    {
        const bool ok = wz::engine::assets::clear_node_behavior_config(
            nodes_, node_id, binding_id, key);
        if (ok) {
            dirty_ = true;
        }
        return { ok, ok ? SceneChange::runtime_rebuild() : SceneChange::none() };
    }

    // --- optional-component mutators (None: no runtime reaction) -------------

    SceneEdit<bool> SceneDocument::add_component(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& kind)
    {
        const bool ok = wz::engine::assets::add_node_optional_component(
            nodes_, node_id, kind);
        if (ok) {
            dirty_ = true;
        }
        return { ok, SceneChange::none() };
    }

    SceneEdit<bool> SceneDocument::remove_component(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& kind)
    {
        const bool ok = wz::engine::assets::remove_node_optional_component(
            nodes_, node_id, kind);
        if (ok) {
            dirty_ = true;
        }
        return { ok, SceneChange::none() };
    }

    SceneEdit<bool> SceneDocument::set_renderable_asset(
        const wz::scene::AuthoredEntityId& node_id,
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id)
    {
        const bool ok = wz::engine::assets::set_node_renderable_asset(
            nodes_, node_id, asset_graph_node_id);
        if (ok) {
            dirty_ = true;
        }
        return { ok, SceneChange::none() };
    }

    SceneEdit<bool> SceneDocument::set_audio_renderable(
        const wz::scene::AuthoredEntityId& node_id,
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id)
    {
        const bool ok = wz::engine::assets::set_node_audio_renderable(
            nodes_, node_id, asset_graph_node_id);
        if (ok) {
            dirty_ = true;
        }
        return { ok, SceneChange::none() };
    }

    SceneEdit<bool> SceneDocument::set_audio_source_play(
        const wz::scene::AuthoredEntityId& node_id,
        bool auto_play,
        bool enabled)
    {
        const bool ok = wz::engine::assets::set_node_audio_source_play(
            nodes_, node_id, auto_play, enabled);
        if (ok) {
            dirty_ = true;
        }
        return { ok, SceneChange::none() };
    }

    // --- renderable / collision / scene-source mutators ----------------------

    SceneEdit<bool> SceneDocument::set_geometry_asset(
        const wz::scene::AuthoredEntityId& node_id,
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id)
    {
        wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(nodes_, node_id);
        if (!node) {
            return { false, SceneChange::none() };
        }
        if (asset_graph_node_id != 0) {
            wz::engine::assets::attach_geometry_asset_node(
                *node, asset_graph_node_id);
        }
        else {
            wz::engine::assets::detach_geometry_asset_node(*node);
            // No geometry => the binding no longer drives this node; drop the
            // renderable it assembled so it stops drawing.
            node->renderable_asset.reset();
        }
        dirty_ = true;
        return { true, SceneChange::render_binding() };
    }

    SceneEdit<bool> SceneDocument::set_collision_asset(
        const wz::scene::AuthoredEntityId& node_id,
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id,
        bool constrain_movement)
    {
        wz::engine::assets::SceneNodeAsset* node =
            wz::engine::assets::find_scene_node(nodes_, node_id);
        if (!node) {
            return { false, SceneChange::none() };
        }
        if (asset_graph_node_id != 0) {
            wz::engine::assets::attach_collision_asset_node(
                *node, asset_graph_node_id, constrain_movement);
        }
        else {
            wz::engine::assets::detach_collision_asset_node(*node);
            // A cleared reference also clears constrain_movement (no surface).
            if (node->collision) {
                node->collision->constrain_movement = constrain_movement;
            }
        }
        dirty_ = true;
        return { true, SceneChange::collision() };
    }

    SceneEdit<bool> SceneDocument::set_scene_source(
        const wz::scene::AuthoredEntityId& node_id,
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id)
    {
        const bool ok = wz::engine::assets::set_node_scene_source(
            nodes_, node_id, asset_graph_node_id);
        if (!ok) {
            return { false, SceneChange::none() };
        }
        dirty_ = true;
        return { true, SceneChange::scene_source() };
    }
}
