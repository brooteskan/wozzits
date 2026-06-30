// tests/asset_scene/prefab_instantiate_tests.cpp
//
// Unit coverage for instantiate_prefab_nodes — the runtime prefab-spawn clone +
// id-remap (the inverse of extract_scene_subtree). Pure over a node span, so it
// tests headless with no app/asset/file dependency.

#include <gtest/gtest.h>

#include <engine/assets/scene/prefab_instantiate.h>

#include <span>
#include <string>
#include <vector>

namespace
{
    using wz::engine::assets::AuthoredTransform;
    using wz::engine::assets::SceneBehaviorAsset;
    using wz::engine::assets::SceneBehaviorConfigValue;
    using wz::engine::assets::SceneBehaviorConfigValueKind;
    using wz::engine::assets::SceneNodeAsset;

    // A 2-node prefab: root + child. The root carries a behavior with a PERSISTED
    // binding id and a config String value that names the child node by authored
    // id (an intra-prefab data reference) plus an asset-graph node-id renderable
    // ref (which must NOT be remapped).
    std::vector<SceneNodeAsset> make_prefab()
    {
        std::vector<SceneNodeAsset> nodes;

        SceneNodeAsset root{};
        root.id = "tank";
        root.local.translation[0] = 99.0f;  // overwritten by root_transform

        SceneBehaviorAsset behavior{};
        behavior.id = "tank/persisted-binding";
        behavior.module = "drive";
        // A config value that names the child node by id -> must be remapped.
        behavior.config.push_back(SceneBehaviorConfigValue{
            .key = "target_node",
            .kind = SceneBehaviorConfigValueKind::String,
            .string_value = "turret",
        });
        // A config value that is NOT a node id -> must be left alone.
        behavior.config.push_back(SceneBehaviorConfigValue{
            .key = "label",
            .kind = SceneBehaviorConfigValueKind::String,
            .string_value = "not-a-node",
        });
        root.behavior = behavior;
        // Asset-graph node-id renderable ref — points at the shared project graph,
        // must survive the clone unchanged.
        root.renderable_asset_node_id = 7u;

        SceneNodeAsset child{};
        child.id = "turret";
        child.parent_id = "tank";
        child.local.translation[1] = 3.0f;  // relative TRS preserved

        nodes.push_back(root);
        nodes.push_back(child);
        return nodes;
    }

    const SceneNodeAsset* find_by_id(
        const std::vector<SceneNodeAsset>& nodes, const std::string& id)
    {
        for (const SceneNodeAsset& n : nodes) {
            if (n.id == id) {
                return &n;
            }
        }
        return nullptr;
    }
}

TEST(PrefabInstantiate, RemapsIdsParentBehaviorAndRoot)
{
    const std::vector<SceneNodeAsset> prefab = make_prefab();

    AuthoredTransform root_transform{};
    root_transform.translation[0] = 10.0f;
    root_transform.translation[1] = 20.0f;
    root_transform.translation[2] = 30.0f;

    const std::vector<SceneNodeAsset> spawned =
        wz::engine::assets::instantiate_prefab_nodes(prefab, 5u, root_transform);

    ASSERT_EQ(spawned.size(), 2u);

    // Ids are remapped to "spawn:5:<orig>".
    const SceneNodeAsset* root = find_by_id(spawned, "spawn:5:tank");
    const SceneNodeAsset* child = find_by_id(spawned, "spawn:5:turret");
    ASSERT_NE(root, nullptr);
    ASSERT_NE(child, nullptr);

    // Parent_id remapped within the subtree.
    ASSERT_TRUE(child->parent_id.has_value());
    EXPECT_EQ(*child->parent_id, "spawn:5:tank");

    // The root is re-rooted: no parent + local == root_transform.
    EXPECT_FALSE(root->parent_id.has_value());
    EXPECT_FLOAT_EQ(root->local.translation[0], 10.0f);
    EXPECT_FLOAT_EQ(root->local.translation[1], 20.0f);
    EXPECT_FLOAT_EQ(root->local.translation[2], 30.0f);

    // Descendant keeps its own relative local TRS.
    EXPECT_FLOAT_EQ(child->local.translation[1], 3.0f);

    // The behavior's persisted binding id is CLEARED (re-derived from new node id).
    ASSERT_TRUE(root->behavior.has_value());
    EXPECT_TRUE(root->behavior->id.empty());

    // The config value naming a prefab node is remapped; the non-node value isn't.
    ASSERT_EQ(root->behavior->config.size(), 2u);
    EXPECT_EQ(root->behavior->config[0].string_value, "spawn:5:turret");
    EXPECT_EQ(root->behavior->config[1].string_value, "not-a-node");

    // Asset-graph node-id renderable ref is unchanged (points at the shared graph).
    ASSERT_TRUE(root->renderable_asset_node_id.has_value());
    EXPECT_EQ(*root->renderable_asset_node_id, 7u);
}

TEST(PrefabInstantiate, DistinctInstanceIndicesYieldDisjointIds)
{
    const std::vector<SceneNodeAsset> prefab = make_prefab();
    const AuthoredTransform t{};

    const std::vector<SceneNodeAsset> a =
        wz::engine::assets::instantiate_prefab_nodes(prefab, 1u, t);
    const std::vector<SceneNodeAsset> b =
        wz::engine::assets::instantiate_prefab_nodes(prefab, 2u, t);

    // No id from instance 1 appears in instance 2 (disjoint id namespaces).
    for (const SceneNodeAsset& na : a) {
        EXPECT_EQ(find_by_id(b, na.id), nullptr)
            << "id '" << na.id << "' collided across instances";
    }
    EXPECT_NE(find_by_id(a, "spawn:1:tank"), nullptr);
    EXPECT_NE(find_by_id(b, "spawn:2:tank"), nullptr);
}
