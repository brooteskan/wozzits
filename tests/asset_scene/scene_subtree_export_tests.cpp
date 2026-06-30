#include "scene_asset_module_test_support.h"

#include <engine/assets/scene/scene_subtree_export.h>

#include <span>
#include <string>
#include <vector>

namespace
{
    using wz::engine::assets::AuthoredTransform;
    using wz::engine::assets::SceneNodeAsset;

    // Build the fixture tree used across these tests:
    //   P (parent, root of the whole scene)
    //   ├── R   (the subtree root we carve out)        translation (5,0,0)
    //   │   └── C1                                     translation (1,2,3)
    //   │       └── G (grandchild)
    //   └── S   (unrelated sibling under P)
    // Extracting "R" must yield exactly {R, C1, G}, excluding P and S.
    std::vector<SceneNodeAsset> make_fixture_nodes()
    {
        std::vector<SceneNodeAsset> nodes;

        SceneNodeAsset p{};
        p.id = "P";
        nodes.push_back(p);

        SceneNodeAsset r{};
        r.id = "R";
        r.parent_id = "P";
        r.local.translation[0] = 5.0f;
        nodes.push_back(r);

        SceneNodeAsset c1{};
        c1.id = "C1";
        c1.parent_id = "R";
        c1.local.translation[0] = 1.0f;
        c1.local.translation[1] = 2.0f;
        c1.local.translation[2] = 3.0f;
        nodes.push_back(c1);

        SceneNodeAsset g{};
        g.id = "G";
        g.parent_id = "C1";
        nodes.push_back(g);

        SceneNodeAsset s{};
        s.id = "S";
        s.parent_id = "P";
        nodes.push_back(s);

        return nodes;
    }

    const SceneNodeAsset* find_node(
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

TEST(SceneSubtreeExport, ExtractsRootAndDescendantsOnly)
{
    const std::vector<SceneNodeAsset> nodes = make_fixture_nodes();

    const auto subtree =
        wz::engine::assets::extract_scene_subtree(
            std::span<const SceneNodeAsset>(nodes), "R");
    ASSERT_TRUE(subtree.has_value());

    EXPECT_EQ(subtree->nodes.size(), 3u);
    EXPECT_NE(find_node(subtree->nodes, "R"), nullptr);
    EXPECT_NE(find_node(subtree->nodes, "C1"), nullptr);
    EXPECT_NE(find_node(subtree->nodes, "G"), nullptr);

    // The parent and the unrelated sibling are excluded.
    EXPECT_EQ(find_node(subtree->nodes, "P"), nullptr);
    EXPECT_EQ(find_node(subtree->nodes, "S"), nullptr);
}

TEST(SceneSubtreeExport, ReRootsSelectedNode)
{
    const std::vector<SceneNodeAsset> nodes = make_fixture_nodes();

    const auto subtree =
        wz::engine::assets::extract_scene_subtree(
            std::span<const SceneNodeAsset>(nodes), "R");
    ASSERT_TRUE(subtree.has_value());

    const SceneNodeAsset* r = find_node(subtree->nodes, "R");
    ASSERT_NE(r, nullptr);

    // The exported root has its parent cleared (a root carries std::nullopt) and
    // its local transform normalized to identity.
    EXPECT_FALSE(r->parent_id.has_value());
    EXPECT_FLOAT_EQ(r->local.translation[0], 0.0f);
    EXPECT_FLOAT_EQ(r->local.translation[1], 0.0f);
    EXPECT_FLOAT_EQ(r->local.translation[2], 0.0f);
    EXPECT_FLOAT_EQ(r->local.rotation_quat[3], 1.0f);
    EXPECT_FLOAT_EQ(r->local.scale[0], 1.0f);
    EXPECT_FLOAT_EQ(r->local.scale[1], 1.0f);
    EXPECT_FLOAT_EQ(r->local.scale[2], 1.0f);

    // Descendant hierarchy stays intact.
    const SceneNodeAsset* c1 = find_node(subtree->nodes, "C1");
    const SceneNodeAsset* g = find_node(subtree->nodes, "G");
    ASSERT_NE(c1, nullptr);
    ASSERT_NE(g, nullptr);
    ASSERT_TRUE(c1->parent_id.has_value());
    ASSERT_TRUE(g->parent_id.has_value());
    EXPECT_EQ(*c1->parent_id, "R");
    EXPECT_EQ(*g->parent_id, "C1");

    // A descendant's own local transform is unchanged.
    EXPECT_FLOAT_EQ(c1->local.translation[0], 1.0f);
    EXPECT_FLOAT_EQ(c1->local.translation[1], 2.0f);
    EXPECT_FLOAT_EQ(c1->local.translation[2], 3.0f);
}

TEST(SceneSubtreeExport, UnknownRootReturnsNullopt)
{
    const std::vector<SceneNodeAsset> nodes = make_fixture_nodes();

    const auto subtree =
        wz::engine::assets::extract_scene_subtree(
            std::span<const SceneNodeAsset>(nodes), "does_not_exist");
    EXPECT_FALSE(subtree.has_value());
}

TEST(SceneSubtreeExport, MalformedParentCycleDoesNotRecurseForever)
{
    // A malformed scene with a parent_id cycle (A<->B) that does NOT involve the
    // root must terminate (not stack-overflow) and exclude the cyclic nodes,
    // since neither reaches the root. The root + its legitimate child survive.
    std::vector<SceneNodeAsset> nodes;

    SceneNodeAsset r{};
    r.id = "R";  // a root (no parent)
    nodes.push_back(r);

    SceneNodeAsset c{};
    c.id = "C";
    c.parent_id = "R";
    nodes.push_back(c);

    SceneNodeAsset a{};
    a.id = "A";
    a.parent_id = "B";
    nodes.push_back(a);

    SceneNodeAsset b{};
    b.id = "B";
    b.parent_id = "A";  // cycle A -> B -> A, disconnected from R
    nodes.push_back(b);

    const auto subtree =
        wz::engine::assets::extract_scene_subtree(
            std::span<const SceneNodeAsset>(nodes), "R");
    ASSERT_TRUE(subtree.has_value());

    EXPECT_EQ(subtree->nodes.size(), 2u);
    EXPECT_NE(find_node(subtree->nodes, "R"), nullptr);
    EXPECT_NE(find_node(subtree->nodes, "C"), nullptr);
    EXPECT_EQ(find_node(subtree->nodes, "A"), nullptr);
    EXPECT_EQ(find_node(subtree->nodes, "B"), nullptr);
}

TEST(SceneSubtreeExport, RoundTripsThroughSceneParser)
{
    // Extract the subtree, serialize it to a scene.json string, then parse it
    // back through the real scene parse path and assert the node set + hierarchy
    // survive — proving the produced document is a valid, reloadable scene.
    const std::vector<SceneNodeAsset> nodes = make_fixture_nodes();
    const auto subtree =
        wz::engine::assets::extract_scene_subtree(
            std::span<const SceneNodeAsset>(nodes), "R");
    ASSERT_TRUE(subtree.has_value());

    wz::json::JSONDocument document =
        wz::engine::assets::export_scene_to_json_document(*subtree);
    const std::string scene_text = wz::json::serialize_json(document);

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_subtree_export_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const auto rel_path =
        write_scene_json(root, "subtree.scene.json", scene_text);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "subtree_prefab",
            .path = rel_path,
        });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data =
        assets.scenes().get_scene_data(
            assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 3u);

    const SceneNodeAsset* r = find_node(scene_data->nodes, "R");
    const SceneNodeAsset* c1 = find_node(scene_data->nodes, "C1");
    const SceneNodeAsset* g = find_node(scene_data->nodes, "G");
    ASSERT_NE(r, nullptr);
    ASSERT_NE(c1, nullptr);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(find_node(scene_data->nodes, "P"), nullptr);
    EXPECT_EQ(find_node(scene_data->nodes, "S"), nullptr);

    // The re-rooted root parses back with no parent; the hierarchy is intact.
    EXPECT_FALSE(r->parent_id.has_value());
    ASSERT_TRUE(c1->parent_id.has_value());
    ASSERT_TRUE(g->parent_id.has_value());
    EXPECT_EQ(*c1->parent_id, "R");
    EXPECT_EQ(*g->parent_id, "C1");
}
