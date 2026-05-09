#include <gtest/gtest.h>

#include <external/toml/toml_parser.h>
#include <external/toml/toml_polytree.h>

#include <graph/static_polytree.h>
#include <graph/static_polytree_algo.h>

#include <string>
#include <vector>

namespace
{
    wz::core::graph::NodeHandle find_table_member(
        const wz::core::graph::Polytree<
        wz::toml::TOMLTreeNode,
        wz::toml::TOMLTreeEdge>& tree,
        wz::core::graph::NodeHandle parent,
        std::string_view key)
    {
        return wz::core::graph::find_child_if(
            tree,
            parent,
            [key](
                wz::core::graph::NodeHandle,
                const wz::toml::TOMLTreeEdge& edge,
                uint32_t)
            {
                return edge.kind == wz::toml::TOMLTreeEdgeKind::TableMember &&
                    edge.key == key;
            }
        );
    }
}

TEST(TOMLPolytree, BuildsTreeForNestedTable)
{
    const auto parsed = wz::toml::parse_toml_string(
        R"(
            [material]
            name = "brick"

            [material.textures]
            albedo = "brick_albedo.png"
        )"
    );

    ASSERT_TRUE(parsed.ok) << parsed.error.message;
    ASSERT_NE(parsed.document.root, nullptr);

    auto storage = wz::toml::build_toml_polytree(parsed.document);
    ASSERT_TRUE(storage.has_value());

    const auto& tree = storage->polytree;

    EXPECT_EQ(wz::core::graph::node_count(tree), 5u);

    constexpr wz::core::graph::NodeHandle root = 0;

    ASSERT_EQ(
        wz::core::graph::node_data(tree, root).value,
        parsed.document.root.get()
    );

    EXPECT_EQ(wz::core::graph::child_count(tree, root), 1u);

    const auto material = find_table_member(tree, root, "material");
    ASSERT_NE(material, wz::core::graph::INVALID_NODE);

    const auto* material_value = wz::core::graph::node_data(tree, material).value;
    ASSERT_NE(material_value, nullptr);
    EXPECT_EQ(material_value->kind, wz::toml::TOMLValueKind::Table);

    const auto name = find_table_member(tree, material, "name");
    ASSERT_NE(name, wz::core::graph::INVALID_NODE);

    const auto textures = find_table_member(tree, material, "textures");
    ASSERT_NE(textures, wz::core::graph::INVALID_NODE);

    const auto albedo = find_table_member(tree, textures, "albedo");
    ASSERT_NE(albedo, wz::core::graph::INVALID_NODE);

    const auto* albedo_value = wz::core::graph::node_data(tree, albedo).value;
    ASSERT_NE(albedo_value, nullptr);
    EXPECT_EQ(albedo_value->kind, wz::toml::TOMLValueKind::String);
    EXPECT_EQ(albedo_value->string_value, "brick_albedo.png");
}

TEST(TOMLPolytree, ArrayChildrenUseOrdinalAsIndex)
{
    const auto parsed = wz::toml::parse_toml_string(
        R"(
            colors = ["red", "green", "blue"]
        )"
    );

    ASSERT_TRUE(parsed.ok) << parsed.error.message;
    ASSERT_NE(parsed.document.root, nullptr);

    auto storage = wz::toml::build_toml_polytree(parsed.document);
    ASSERT_TRUE(storage.has_value());

    const auto& tree = storage->polytree;

    constexpr wz::core::graph::NodeHandle root = 0;

    const auto colors = find_table_member(tree, root, "colors");
    ASSERT_NE(colors, wz::core::graph::INVALID_NODE);

    ASSERT_EQ(wz::core::graph::child_count(tree, colors), 3u);

    for (uint32_t i = 0; i < 3; ++i) {
        const auto child = wz::core::graph::child_at(tree, colors, i);
        ASSERT_NE(child, wz::core::graph::INVALID_NODE);

        const auto* edge = wz::core::graph::child_edge_data_at(tree, colors, i);

        ASSERT_NE(edge, nullptr);
        EXPECT_EQ(edge->kind, wz::toml::TOMLTreeEdgeKind::ArrayElement);
        EXPECT_TRUE(edge->key.empty());

        EXPECT_EQ(wz::core::graph::child_ordinal(tree, child), i);
    }
}

TEST(TOMLPolytree, WalksPathFromRoot)
{
    const auto parsed = wz::toml::parse_toml_string(
        R"(
            [[materials]]
            name = "brick"

            [materials.textures]
            albedo = "brick_albedo.png"
        )"
    );

    ASSERT_TRUE(parsed.ok) << parsed.error.message;
    ASSERT_NE(parsed.document.root, nullptr);

    auto storage = wz::toml::build_toml_polytree(parsed.document);
    ASSERT_TRUE(storage.has_value());

    const auto& tree = storage->polytree;

    constexpr wz::core::graph::NodeHandle root = 0;

    const auto materials = find_table_member(tree, root, "materials");
    ASSERT_NE(materials, wz::core::graph::INVALID_NODE);

    const auto material0 = wz::core::graph::child_at(tree, materials, 0);
    ASSERT_NE(material0, wz::core::graph::INVALID_NODE);

    const auto textures = find_table_member(tree, material0, "textures");
    ASSERT_NE(textures, wz::core::graph::INVALID_NODE);

    const auto albedo = find_table_member(tree, textures, "albedo");
    ASSERT_NE(albedo, wz::core::graph::INVALID_NODE);

    std::vector<wz::core::graph::NodeHandle> scratch(
        wz::core::graph::node_count(tree)
    );

    std::vector<std::string> path_parts;

    const bool ok = wz::core::graph::walk_path_from_root(
        tree,
        albedo,
        scratch,
        [&](wz::core::graph::NodeHandle,
            wz::core::graph::NodeHandle,
            const wz::toml::TOMLTreeEdge& edge,
            uint32_t ordinal)
        {
            if (edge.kind == wz::toml::TOMLTreeEdgeKind::TableMember) {
                path_parts.push_back("." + std::string(edge.key));
            }
            else {
                path_parts.push_back("[" + std::to_string(ordinal) + "]");
            }
        }
    );

    ASSERT_TRUE(ok);

    ASSERT_EQ(path_parts.size(), 4u);
    EXPECT_EQ(path_parts[0], ".materials");
    EXPECT_EQ(path_parts[1], "[0]");
    EXPECT_EQ(path_parts[2], ".textures");
    EXPECT_EQ(path_parts[3], ".albedo");
}