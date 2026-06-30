#pragma once

// engine/assets/scene/prefab_instantiate.h

#include <engine/assets/scene/scene_asset_data.h>

#include <cstdint>
#include <span>
#include <vector>

namespace wz::engine::assets
{
    // Clone a prefab ("scenelet" Scene asset) into a fresh, conflict-free node
    // subtree ready to graft into a running scene (the second milestone of the
    // prefab system: runtime spawning). The inverse of extract_scene_subtree —
    // that carves a self-contained subtree OUT of a scene; this stamps a copy of
    // such a subtree back IN, under a spawn transform, with unique ids so it never
    // collides with the host scene or another instance.
    //
    // Every prefab node's authored id is remapped to "spawn:<instance_index>:" +
    // its original id, so two spawns of the same prefab (distinct instance_index)
    // yield disjoint id sets. The remap is applied to:
    //   - each node's own id,
    //   - each node's parent_id (it points within the prefab for a self-contained
    //     scenelet), and
    //   - any DATA reference that names a remapped node by authored id — notably a
    //     behavior config String value whose text equals a remapped id (so an
    //     intra-prefab node reference still points within the spawned copy).
    // Asset-graph node-id references (renderable / geometry / render-program /
    // scene-source / collision / audio-renderable graph nodes) are NOT touched:
    // those point at the shared project asset graph, not at scene nodes.
    //
    // Each behavior's persisted binding id is cleared so effective_behavior_-
    // binding_id re-derives a unique binding id from the new (remapped) node id;
    // this gives every spawned instance independent behavior state.
    //
    // The prefab ROOT (the node with no parent within the set) is re-rooted: its
    // parent_id is cleared (it becomes a top-level node under the spawn) and its
    // local transform is set to `root_transform` (T = spawner world × offset);
    // descendants keep their relative local TRS.
    //
    // Pure over the input span — no app/asset/file dependency — so it unit-tests
    // headless. Returns the cloned, remapped nodes in input order.
    std::vector<SceneNodeAsset> instantiate_prefab_nodes(
        std::span<const SceneNodeAsset> prefab_nodes,
        uint32_t instance_index,
        const AuthoredTransform& root_transform);

} // namespace wz::engine::assets
