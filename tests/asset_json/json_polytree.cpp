#include <gtest/gtest.h>

#include <string>
#include <vector>

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

TEST(JSONPolytree, WalksPathFromRoot)
{
    const auto parsed = wz::json::parse_json_string(
        R"({
            "materials": [
                {
                    "textures": {
                        "albedo": "brick_albedo.png"
                    }
                }
            ]
        })"
    );

    ASSERT_TRUE(parsed.ok);
    ASSERT_NE(parsed.document.root, nullptr);

    auto storage = wz::json::build_json_polytree(parsed.document);
    ASSERT_TRUE(storage.has_value());

    const auto& tree = storage->polytree;

    constexpr wz::core::graph::NodeHandle root = 0;

    const auto materials_node =
        wz::core::graph::find_child_if(
            tree,
            root,
            [](wz::core::graph::NodeHandle,
                const wz::json::JSONTreeEdge& edge,
                uint32_t)
            {
                return edge.kind == wz::json::JSONTreeEdgeKind::ObjectMember &&
                    edge.key == "materials";
            }
        );

    ASSERT_NE(materials_node, wz::core::graph::INVALID_NODE);

    const auto material0_node =
        wz::core::graph::child_at(tree, materials_node, 0);

    ASSERT_NE(material0_node, wz::core::graph::INVALID_NODE);

    const auto textures_node =
        wz::core::graph::find_child_if(
            tree,
            material0_node,
            [](wz::core::graph::NodeHandle,
                const wz::json::JSONTreeEdge& edge,
                uint32_t)
            {
                return edge.kind == wz::json::JSONTreeEdgeKind::ObjectMember &&
                    edge.key == "textures";
            }
        );

    ASSERT_NE(textures_node, wz::core::graph::INVALID_NODE);

    const auto albedo_node =
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

    std::vector<wz::core::graph::NodeHandle> scratch(
        wz::core::graph::node_count(tree)
    );

    std::vector<std::string> path_parts;

    const bool ok = wz::core::graph::walk_path_from_root(
        tree,
        albedo_node,
        scratch,
        [&](wz::core::graph::NodeHandle,
            wz::core::graph::NodeHandle,
            const wz::json::JSONTreeEdge& edge,
            uint32_t ordinal)
        {
            if (edge.kind == wz::json::JSONTreeEdgeKind::ObjectMember) {
                path_parts.push_back("." + std::string(edge.key));
            }
            else {
                path_parts.push_back(
                    "[" + std::to_string(ordinal) + "]"
                );
            }
        }
    );

    ASSERT_TRUE(ok);

    ASSERT_EQ(path_parts.size(), 4u);
    EXPECT_EQ(path_parts[0], ".materials");
    EXPECT_EQ(path_parts[1], "[0]");
    EXPECT_EQ(path_parts[2], ".textures");
    EXPECT_EQ(path_parts[3], ".albedo");

    const auto* albedo_value =
        wz::core::graph::node_data(tree, albedo_node).value;

    ASSERT_NE(albedo_value, nullptr);
    ASSERT_EQ(albedo_value->kind, wz::json::JSONValueKind::String);
    EXPECT_EQ(albedo_value->string_value, "brick_albedo.png");
}

TEST(JSONPolytree, EmptyDocumentReturnsNullopt)
{
    wz::json::JSONDocument document{};

    auto storage = wz::json::build_json_polytree(document);

    EXPECT_FALSE(storage.has_value());
}

TEST(JSONPolytree, WalkPathFromRootFailsWhenScratchTooSmall)
{
    const auto parsed = wz::json::parse_json_string(
        R"({
            "materials": [
                {
                    "textures": {
                        "albedo": "brick_albedo.png"
                    }
                }
            ]
        })"
    );

    ASSERT_TRUE(parsed.ok);
    ASSERT_NE(parsed.document.root, nullptr);

    auto storage = wz::json::build_json_polytree(parsed.document);
    ASSERT_TRUE(storage.has_value());

    const auto& tree = storage->polytree;

    constexpr wz::core::graph::NodeHandle root = 0;

    const auto materials_node =
        wz::core::graph::find_child_if(
            tree,
            root,
            [](wz::core::graph::NodeHandle,
                const wz::json::JSONTreeEdge& edge,
                uint32_t)
            {
                return edge.kind == wz::json::JSONTreeEdgeKind::ObjectMember &&
                    edge.key == "materials";
            }
        );

    ASSERT_NE(materials_node, wz::core::graph::INVALID_NODE);

    const auto material0_node =
        wz::core::graph::child_at(tree, materials_node, 0);

    ASSERT_NE(material0_node, wz::core::graph::INVALID_NODE);

    const auto textures_node =
        wz::core::graph::find_child_if(
            tree,
            material0_node,
            [](wz::core::graph::NodeHandle,
                const wz::json::JSONTreeEdge& edge,
                uint32_t)
            {
                return edge.kind == wz::json::JSONTreeEdgeKind::ObjectMember &&
                    edge.key == "textures";
            }
        );

    ASSERT_NE(textures_node, wz::core::graph::INVALID_NODE);

    const auto albedo_node =
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

    // Full path needs root + materials + material[0] + textures + albedo = 5 slots.
    std::vector<wz::core::graph::NodeHandle> scratch(4);

    bool visitor_called = false;

    const bool ok = wz::core::graph::walk_path_from_root(
        tree,
        albedo_node,
        scratch,
        [&](wz::core::graph::NodeHandle,
            wz::core::graph::NodeHandle,
            const wz::json::JSONTreeEdge&,
            uint32_t)
        {
            visitor_called = true;
        }
    );

    EXPECT_FALSE(ok);
    EXPECT_FALSE(visitor_called);
}

TEST(JSONPolytree, WalkPathFromRootOnRootSucceedsAndVisitsNoEdges)
{
    const auto parsed = wz::json::parse_json_string(
        R"({ "name": "brick" })"
    );

    ASSERT_TRUE(parsed.ok);
    ASSERT_NE(parsed.document.root, nullptr);

    auto storage = wz::json::build_json_polytree(parsed.document);
    ASSERT_TRUE(storage.has_value());

    const auto& tree = storage->polytree;

    constexpr wz::core::graph::NodeHandle root = 0;

    std::vector<wz::core::graph::NodeHandle> scratch(
        wz::core::graph::node_count(tree)
    );

    uint32_t visited_edges = 0;

    const bool ok = wz::core::graph::walk_path_from_root(
        tree,
        root,
        scratch,
        [&](wz::core::graph::NodeHandle,
            wz::core::graph::NodeHandle,
            const wz::json::JSONTreeEdge&,
            uint32_t)
        {
            ++visited_edges;
        }
    );

    EXPECT_TRUE(ok);
    EXPECT_EQ(visited_edges, 0u);
}

TEST(JSONPolytree, ArrayChildrenUseOrdinalAsIndex)
{
    const auto parsed = wz::json::parse_json_string(
        R"(["red", "green", "blue"])"
    );

    ASSERT_TRUE(parsed.ok);
    ASSERT_NE(parsed.document.root, nullptr);

    auto storage = wz::json::build_json_polytree(parsed.document);
    ASSERT_TRUE(storage.has_value());

    const auto& tree = storage->polytree;

    constexpr wz::core::graph::NodeHandle root = 0;

    ASSERT_EQ(wz::core::graph::child_count(tree, root), 3u);

    for (uint32_t i = 0; i < 3; ++i) {
        const auto child = wz::core::graph::child_at(tree, root, i);
        ASSERT_NE(child, wz::core::graph::INVALID_NODE);

        const auto* edge =
            wz::core::graph::child_edge_data_at(tree, root, i);

        ASSERT_NE(edge, nullptr);
        EXPECT_EQ(edge->kind, wz::json::JSONTreeEdgeKind::ArrayElement);
        EXPECT_TRUE(edge->key.empty());

        EXPECT_EQ(wz::core::graph::child_ordinal(tree, child), i);
    }

    EXPECT_EQ(
        wz::core::graph::child_at(tree, root, 3),
        wz::core::graph::INVALID_NODE
    );

    EXPECT_EQ(
        wz::core::graph::child_edge_data_at(tree, root, 3),
        nullptr
    );
}

TEST(JSONPolytree, SiblingNavigationWorksForObjectMembers)
{
    const auto parsed = wz::json::parse_json_string(
        R"({
            "a": 1,
            "b": 2,
            "c": 3
        })"
    );

    ASSERT_TRUE(parsed.ok);
    ASSERT_NE(parsed.document.root, nullptr);

    auto storage = wz::json::build_json_polytree(parsed.document);
    ASSERT_TRUE(storage.has_value());

    const auto& tree = storage->polytree;

    constexpr wz::core::graph::NodeHandle root = 0;

    const auto a = wz::core::graph::child_at(tree, root, 0);
    const auto b = wz::core::graph::child_at(tree, root, 1);
    const auto c = wz::core::graph::child_at(tree, root, 2);

    ASSERT_NE(a, wz::core::graph::INVALID_NODE);
    ASSERT_NE(b, wz::core::graph::INVALID_NODE);
    ASSERT_NE(c, wz::core::graph::INVALID_NODE);

    EXPECT_EQ(wz::core::graph::previous_sibling(tree, a),
        wz::core::graph::INVALID_NODE);
    EXPECT_EQ(wz::core::graph::next_sibling(tree, a), b);

    EXPECT_EQ(wz::core::graph::previous_sibling(tree, b), a);
    EXPECT_EQ(wz::core::graph::next_sibling(tree, b), c);

    EXPECT_EQ(wz::core::graph::previous_sibling(tree, c), b);
    EXPECT_EQ(wz::core::graph::next_sibling(tree, c),
        wz::core::graph::INVALID_NODE);

    EXPECT_EQ(wz::core::graph::previous_sibling(tree, root),
        wz::core::graph::INVALID_NODE);
    EXPECT_EQ(wz::core::graph::next_sibling(tree, root),
        wz::core::graph::INVALID_NODE);
}

TEST(JSONPolytree, DepthMatchesNestedJSONStructure)
{
    const auto parsed = wz::json::parse_json_string(
        R"({
            "a": {
                "b": [
                    {
                        "c": true
                    }
                ]
            }
        })"
    );

    ASSERT_TRUE(parsed.ok);
    ASSERT_NE(parsed.document.root, nullptr);

    auto storage = wz::json::build_json_polytree(parsed.document);
    ASSERT_TRUE(storage.has_value());

    const auto& tree = storage->polytree;

    constexpr wz::core::graph::NodeHandle root = 0;

    const auto a =
        wz::core::graph::find_child_if(
            tree,
            root,
            [](wz::core::graph::NodeHandle,
                const wz::json::JSONTreeEdge& edge,
                uint32_t)
            {
                return edge.kind == wz::json::JSONTreeEdgeKind::ObjectMember &&
                    edge.key == "a";
            }
        );

    ASSERT_NE(a, wz::core::graph::INVALID_NODE);

    const auto b =
        wz::core::graph::find_child_if(
            tree,
            a,
            [](wz::core::graph::NodeHandle,
                const wz::json::JSONTreeEdge& edge,
                uint32_t)
            {
                return edge.kind == wz::json::JSONTreeEdgeKind::ObjectMember &&
                    edge.key == "b";
            }
        );

    ASSERT_NE(b, wz::core::graph::INVALID_NODE);

    const auto b0 = wz::core::graph::child_at(tree, b, 0);
    ASSERT_NE(b0, wz::core::graph::INVALID_NODE);

    const auto c =
        wz::core::graph::find_child_if(
            tree,
            b0,
            [](wz::core::graph::NodeHandle,
                const wz::json::JSONTreeEdge& edge,
                uint32_t)
            {
                return edge.kind == wz::json::JSONTreeEdgeKind::ObjectMember &&
                    edge.key == "c";
            }
        );

    ASSERT_NE(c, wz::core::graph::INVALID_NODE);

    EXPECT_EQ(wz::core::graph::depth(tree, root), 0u);
    EXPECT_EQ(wz::core::graph::depth(tree, a), 1u);
    EXPECT_EQ(wz::core::graph::depth(tree, b), 2u);
    EXPECT_EQ(wz::core::graph::depth(tree, b0), 3u);
    EXPECT_EQ(wz::core::graph::depth(tree, c), 4u);
}

