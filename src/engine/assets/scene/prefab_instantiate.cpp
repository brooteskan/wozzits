// src/engine/assets/scene/prefab_instantiate.cpp

#include <engine/assets/scene/prefab_instantiate.h>

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace wz::engine::assets
{
    namespace
    {
        std::string remapped_id(
            uint32_t instance_index,
            const std::string& orig_id)
        {
            return "spawn:" + std::to_string(instance_index) + ":" + orig_id;
        }

        // Rewrite a behavior's persisted node references and clear its binding id.
        // The only DATA reference a behavior can carry to a scene node is a config
        // String value naming a node by authored id, so rewrite a config value
        // that exactly matches a remapped id; the binding id is cleared so a fresh
        // one is re-derived from the new node id (independent per-instance state).
        void remap_behavior(
            SceneBehaviorAsset& behavior,
            const std::unordered_map<std::string, std::string>& id_remap)
        {
            behavior.id.clear();
            for (SceneBehaviorConfigValue& value : behavior.config) {
                if (value.kind != SceneBehaviorConfigValueKind::String) {
                    continue;
                }
                const auto it = id_remap.find(value.string_value);
                if (it != id_remap.end()) {
                    value.string_value = it->second;
                }
            }
        }
    }

    std::vector<SceneNodeAsset> instantiate_prefab_nodes(
        std::span<const SceneNodeAsset> prefab_nodes,
        uint32_t instance_index,
        const AuthoredTransform& root_transform)
    {
        // Build the id remap for every prefab node up front: a self-contained
        // scenelet references nodes only by their own ids, so this map is the
        // authority for rewriting both structural (parent_id) and data
        // (behavior-param) references. The set of prefab ids also tells a
        // parent_id whether it points within the prefab (it always should).
        std::unordered_map<std::string, std::string> id_remap;
        id_remap.reserve(prefab_nodes.size());
        for (const SceneNodeAsset& node : prefab_nodes) {
            id_remap.emplace(node.id, remapped_id(instance_index, node.id));
        }

        std::vector<SceneNodeAsset> spawned;
        spawned.reserve(prefab_nodes.size());

        for (const SceneNodeAsset& source : prefab_nodes) {
            SceneNodeAsset node = source;  // deep copy

            // Remap this node's own id.
            const auto self_it = id_remap.find(node.id);
            node.id = self_it != id_remap.end()
                ? self_it->second
                : remapped_id(instance_index, node.id);

            // Remap parent_id when it points within the prefab. The root (no
            // parent, or a parent outside the set) is re-rooted below.
            const bool is_root =
                !node.parent_id
                || id_remap.find(*node.parent_id) == id_remap.end();
            if (is_root) {
                node.parent_id.reset();
                node.local = root_transform;
            } else {
                node.parent_id = id_remap.at(*node.parent_id);
            }

            // Remap behavior node references + clear binding ids so each spawned
            // instance gets its own behavior state. Asset-graph node-id refs
            // (renderable/geometry/render-program/scene-source/collision/audio)
            // are left untouched — they point at the shared project graph.
            if (node.behavior) {
                remap_behavior(*node.behavior, id_remap);
            }
            for (SceneBehaviorAsset& behavior : node.behaviors) {
                remap_behavior(behavior, id_remap);
            }

            spawned.push_back(std::move(node));
        }

        return spawned;
    }

} // namespace wz::engine::assets
