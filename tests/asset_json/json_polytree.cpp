#include <gtest/gtest.h>

#include <external/json/json_parser.h>
#include <external/json/json_polytree.h>

#include <graph/static_polytree.h>
#include <graph/static_polytree_algo.h>

TEST(JSONPolytree, BuildsTreeForObject)
{
    const auto parsed = wz::json::parse_json_string(
        R"({
            "name": "brick",
            "textures": {
                "albedo": "brick_albedo.png"
            }
        })"
    );

    ASSERT_TRUE(parsed.ok);
    ASSERT_NE(parsed.document.root, nullptr);

    auto storage = wz::json::build_json_polytree(parsed.document);

    ASSERT_TRUE(storage.has_value());

    const auto& tree = storage->polytree;

    EXPECT_EQ(wz::core::graph::node_count(tree), 4u);

    constexpr wz::core::graph::NodeHandle root = 0;

    ASSERT_EQ(
        wz::core::graph::node_data(tree, root).value,
        parsed.document.root.get()
    );

    EXPECT_EQ(wz::core::graph::child_count(tree, root), 2u);

    const auto name_node =
        wz::core::graph::child_at(tree, root, 0);

    const auto textures_node =
        wz::core::graph::child_at(tree, root, 1);

    ASSERT_NE(name_node, wz::core::graph::INVALID_NODE);
    ASSERT_NE(textures_node, wz::core::graph::INVALID_NODE);

    const auto* name_edge =
        wz::core::graph::child_edge_data_at(tree, root, 0);

    const auto* textures_edge =
        wz::core::graph::child_edge_data_at(tree, root, 1);

    ASSERT_NE(name_edge, nullptr);
    ASSERT_NE(textures_edge, nullptr);

    EXPECT_EQ(name_edge->kind, wz::json::JSONTreeEdgeKind::ObjectMember);
    EXPECT_EQ(name_edge->key, "name");

    EXPECT_EQ(textures_edge->kind, wz::json::JSONTreeEdgeKind::ObjectMember);
    EXPECT_EQ(textures_edge->key, "textures");

    ASSERT_NE(wz::core::graph::node_data(tree, name_node).value, nullptr);
    EXPECT_EQ(
        wz::core::graph::node_data(tree, name_node).value->kind,
        wz::json::JSONValueKind::String
    );

    ASSERT_NE(wz::core::graph::node_data(tree, textures_node).value, nullptr);
    EXPECT_EQ(
        wz::core::graph::node_data(tree, textures_node).value->kind,
        wz::json::JSONValueKind::Object
    );
}

TEST(JSONPolytree, FindsObjectMemberByEdgeKey)
{
    const auto parsed = wz::json::parse_json_string(
        R"({
            "name": "brick",
            "textures": {
                "albedo": "brick_albedo.png",
                "normal": "brick_normal.png"
            },
            "roughness": 0.8
        })"
    );

    ASSERT_TRUE(parsed.ok);
    ASSERT_NE(parsed.document.root, nullptr);

    auto storage = wz::json::build_json_polytree(parsed.document);

    ASSERT_TRUE(storage.has_value());

    const auto& tree = storage->polytree;

    constexpr wz::core::graph::NodeHandle root = 0;

    const wz::core::graph::NodeHandle textures_node =
        wz::core::graph::find_child_if(
            tree,
            root,
            [](wz::core::graph::NodeHandle,
                const wz::json::JSONTreeEdge& edge,
                uint32_t)
            {
                return edge.kind == wz::json::JSONTreeEdgeKind::ObjectMember &&
                    edge.key == "textures";
            }
        );

    ASSERT_NE(textures_node, wz::core::graph::INVALID_NODE);

    const auto* textures_value =
        wz::core::graph::node_data(tree, textures_node).value;

    ASSERT_NE(textures_value, nullptr);
    EXPECT_EQ(textures_value->kind, wz::json::JSONValueKind::Object);

    const wz::core::graph::NodeHandle albedo_node =
        wz::core::graph::find_child_if(
            tree,
            textures_node,
            [](wz::core::graph::NodeHandle,
                const wz::json::JSONTreeEdge& edge,
                uint32_t)
            {
                return edge.kind == wz::json::JSONTreeEdgeKind::ObjectMember &&
                    edge.key == "albedo";
            }
        );

    ASSERT_NE(albedo_node, wz::core::graph::INVALID_NODE);

    const auto* albedo_value =
        wz::core::graph::node_data(tree, albedo_node).value;

    ASSERT_NE(albedo_value, nullptr);
    EXPECT_EQ(albedo_value->kind, wz::json::JSONValueKind::String);
    EXPECT_EQ(albedo_value->string_value, "brick_albedo.png");

    const wz::core::graph::NodeHandle missing_node =
        wz::core::graph::find_child_if(
            tree,
            textures_node,
            [](wz::core::graph::NodeHandle,
                const wz::json::JSONTreeEdge& edge,
                uint32_t)
            {
                return edge.kind == wz::json::JSONTreeEdgeKind::ObjectMember &&
                    edge.key == "height";
            }
        );

    EXPECT_EQ(missing_node, wz::core::graph::INVALID_NODE);
}