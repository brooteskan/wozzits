// src/engine/assets/authoring/scene_authoring.cpp

#include <engine/assets/authoring/scene_authoring.h>

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace wz::engine::assets::authoring
{
    bool scene_node_exists(
        const SceneAssetData& scene,
        const SceneNodeId& id)
    {
        return find_scene_node(scene, id) != nullptr;
    }

    bool is_scene_node_descendant_of(
        const SceneAssetData& scene,
        const SceneNodeId& node,
        const SceneNodeId& ancestor)
    {
        const SceneNodeAsset* current = find_scene_node(scene, node);
        while (current && current->parent_id) {
            if (*current->parent_id == ancestor) {
                return true;
            }
            current = find_scene_node(scene, *current->parent_id);
        }
        return false;
    }

    void collect_scene_subtree_ids(
        const SceneAssetData& scene,
        const SceneNodeId& root,
        std::vector<SceneNodeId>& out)
    {
        if (std::find(out.begin(), out.end(), root) != out.end()) {
            return;
        }
        out.push_back(root);
        for (const SceneNodeAsset& node : scene.nodes) {
            if (node.parent_id && *node.parent_id == root) {
                collect_scene_subtree_ids(scene, node.id, out);
            }
        }
    }

    bool add_scene_node(
        SceneAssetData& scene,
        SceneNodeId id,
        std::optional<SceneNodeId> parent_id,
        std::string name)
    {
        if (id.empty() || scene_node_exists(scene, id)) {
            return false;
        }
        if (parent_id
            && (*parent_id == id || !scene_node_exists(scene, *parent_id)))
        {
            return false;
        }

        SceneNodeAsset node{};
        node.id = std::move(id);
        node.parent_id = std::move(parent_id);
        node.name = std::move(name);
        node.visible = true;
        scene.nodes.push_back(std::move(node));
        return true;
    }

    std::vector<SceneNodeId> remove_scene_node(
        SceneAssetData& scene,
        const SceneNodeId& id)
    {
        std::vector<SceneNodeId> removed;
        if (!scene_node_exists(scene, id)) {
            return removed;
        }

        collect_scene_subtree_ids(scene, id, removed);
        const std::unordered_set<SceneNodeId> removed_set(
            removed.begin(),
            removed.end());

        scene.nodes.erase(
            std::remove_if(
                scene.nodes.begin(),
                scene.nodes.end(),
                [&removed_set](const SceneNodeAsset& node)
                {
                    return removed_set.count(node.id) != 0;
                }),
            scene.nodes.end());

        if (scene.defaults.active_camera_node
            && removed_set.count(*scene.defaults.active_camera_node) != 0)
        {
            scene.defaults.active_camera_node.reset();
        }

        for (SceneNodeAsset& node : scene.nodes) {
            if (node.terrain_render_style) {
                SceneTerrainRenderStyleAsset& style = *node.terrain_render_style;
                if (removed_set.count(style.directional_light_node) != 0) {
                    style.directional_light_node.clear();
                }
                if (removed_set.count(style.ambient_light_node) != 0) {
                    style.ambient_light_node.clear();
                }
                if (removed_set.count(style.environment_node) != 0) {
                    style.environment_node.clear();
                }
            }
            if (node.terrain_mesh_source
                && removed_set.count(node.terrain_mesh_source->source_node) != 0)
            {
                node.terrain_mesh_source->source_node.clear();
            }
            if (node.terrain_height_field_source
                && removed_set.count(
                       node.terrain_height_field_source->source_node) != 0)
            {
                node.terrain_height_field_source->source_node.clear();
            }
        }

        return removed;
    }

    bool reparent_scene_node(
        SceneAssetData& scene,
        const SceneNodeId& id,
        std::optional<SceneNodeId> new_parent_id)
    {
        SceneNodeAsset* node = find_scene_node(scene, id);
        if (!node) {
            return false;
        }

        if (new_parent_id) {
            if (*new_parent_id == id
                || !scene_node_exists(scene, *new_parent_id)
                || is_scene_node_descendant_of(scene, *new_parent_id, id))
            {
                return false;
            }
            node->parent_id = std::move(*new_parent_id);
        }
        else {
            node->parent_id.reset();
        }
        return true;
    }

    bool rename_scene_node(
        SceneAssetData& scene,
        const SceneNodeId& id,
        std::string name)
    {
        SceneNodeAsset* node = find_scene_node(scene, id);
        if (!node) {
            return false;
        }
        node->name = std::move(name);
        return true;
    }

    bool set_node_transform(
        SceneAssetData& scene,
        const SceneNodeId& id,
        const AuthoredTransform& transform)
    {
        SceneNodeAsset* node = find_scene_node(scene, id);
        if (!node) {
            return false;
        }
        node->local = transform;
        return true;
    }

    bool set_node_translation(
        SceneAssetData& scene,
        const SceneNodeId& id,
        std::array<float, 3> translation)
    {
        SceneNodeAsset* node = find_scene_node(scene, id);
        if (!node) {
            return false;
        }
        node->local.translation[0] = translation[0];
        node->local.translation[1] = translation[1];
        node->local.translation[2] = translation[2];
        return true;
    }

    bool set_node_rotation_quat(
        SceneAssetData& scene,
        const SceneNodeId& id,
        std::array<float, 4> rotation_quat)
    {
        SceneNodeAsset* node = find_scene_node(scene, id);
        if (!node) {
            return false;
        }
        node->local.rotation_quat[0] = rotation_quat[0];
        node->local.rotation_quat[1] = rotation_quat[1];
        node->local.rotation_quat[2] = rotation_quat[2];
        node->local.rotation_quat[3] = rotation_quat[3];
        return true;
    }

    bool set_node_scale(
        SceneAssetData& scene,
        const SceneNodeId& id,
        std::array<float, 3> scale)
    {
        SceneNodeAsset* node = find_scene_node(scene, id);
        if (!node) {
            return false;
        }
        node->local.scale[0] = scale[0];
        node->local.scale[1] = scale[1];
        node->local.scale[2] = scale[2];
        return true;
    }

    bool set_node_visible(
        SceneAssetData& scene,
        const SceneNodeId& id,
        bool visible)
    {
        SceneNodeAsset* node = find_scene_node(scene, id);
        if (!node) {
            return false;
        }
        node->visible = visible;
        return true;
    }

    bool set_node_motion_type(
        SceneAssetData& scene,
        const SceneNodeId& id,
        wz::scene::TransformNode::MotionType motion_type)
    {
        SceneNodeAsset* node = find_scene_node(scene, id);
        if (!node) {
            return false;
        }
        node->motion_type = motion_type;
        return true;
    }

    bool assign_node_renderable(
        SceneAssetData& scene,
        const SceneNodeId& id,
        wz::asset::AssetGraphDraftNodeId graph_node,
        wz::asset::AssetKey resolved)
    {
        // Reject the sentinel so a binding always names a real authored graph
        // node; callers clear a binding via clear_node_renderable instead.
        if (graph_node == wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE) {
            return false;
        }
        SceneNodeAsset* node = find_scene_node(scene, id);
        if (!node) {
            return false;
        }
        attach_renderable_asset_node(*node, graph_node, resolved);
        return true;
    }

    bool clear_node_renderable(
        SceneAssetData& scene,
        const SceneNodeId& id)
    {
        SceneNodeAsset* node = find_scene_node(scene, id);
        if (!node) {
            return false;
        }
        detach_renderable_asset_node(*node);
        return true;
    }
}
