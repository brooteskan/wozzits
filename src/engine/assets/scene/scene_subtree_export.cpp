// src/engine/assets/scene/scene_subtree_export.cpp

#include <engine/assets/scene/scene_subtree_export.h>

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace wz::engine::assets
{
    std::optional<SceneAssetData> extract_scene_subtree(
        std::span<const SceneNodeAsset> nodes,
        std::string_view root_id)
    {
        // Index every node by id so the ancestor walk is O(depth), and confirm
        // the root exists up front (a missing root => no subtree to extract).
        std::unordered_map<std::string_view, const SceneNodeAsset*> by_id;
        by_id.reserve(nodes.size());
        for (const SceneNodeAsset& node : nodes) {
            by_id.emplace(std::string_view(node.id), &node);
        }
        if (by_id.find(root_id) == by_id.end()) {
            return std::nullopt;
        }

        // A node is in the subtree iff walking its parent_id chain reaches the
        // root before leaving `nodes` (a broken/cyclic chain bails out as not
        // included). Each walk memoizes the verdict for every id it touched so a
        // deep tree resolves in roughly one pass.
        std::unordered_map<std::string_view, bool> under_root;
        const auto reaches_root =
            [&](std::string_view id, auto&& self) -> bool {
                if (id == root_id) {
                    return true;
                }
                if (const auto it = under_root.find(id);
                    it != under_root.end())
                {
                    return it->second;
                }

                const auto node_it = by_id.find(id);
                if (node_it == by_id.end()) {
                    return false;  // id not in this span — not under the root
                }
                const SceneNodeAsset& node = *node_it->second;
                // Mark this id in-progress (not-under-root) BEFORE recursing, so a
                // malformed parent_id cycle that re-enters this id terminates via
                // the memo above instead of recursing forever. Scene hierarchies
                // are trees, so a node is never legitimately revisited in one walk
                // — only a cycle hits this, and it correctly resolves to false.
                under_root[id] = false;
                bool result = false;
                if (node.parent_id) {
                    result = self(std::string_view(*node.parent_id), self);
                }
                under_root[id] = result;
                return result;
            };

        // Collect the root + every transitive descendant, preserving the input
        // order so the exported scene reads like the source subtree.
        SceneAssetData subtree;
        for (const SceneNodeAsset& node : nodes) {
            if (reaches_root(std::string_view(node.id), reaches_root)) {
                subtree.nodes.push_back(node);
            }
        }

        // Re-root: the exported root sits at the origin so a future spawn places
        // the prefab purely by the spawn transform. Clear its parent (a root
        // carries std::nullopt) and normalize its local TRS to identity;
        // descendants keep their own local TRS (still correct relative to the
        // now-identity root) and their parent_id's (all within the subtree).
        for (SceneNodeAsset& node : subtree.nodes) {
            if (node.id == root_id) {
                node.parent_id.reset();
                node.local = AuthoredTransform{};
                break;
            }
        }

        return subtree;
    }

} // namespace wz::engine::assets
