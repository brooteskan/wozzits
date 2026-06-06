#include <gtest/gtest.h>
#include <graph/graph_problems.h>

using namespace wz::core::graph;

// ─── Fixtures ─────────────────────────────────────────────────────────────────
//
// Weighted DAG (see diagram):
//
//          0
//        /   \
//       1     2       edge weights shown in diagram
//      / \     \
//     3   4     4
//      \  |    /
//        5
//
// Edges and costs:
//   0→1 w=3, 0→2 w=1
//   1→3 w=4, 1→4 w=2
//   2→4 w=5
//   3→5 w=2, 4→5 w=1
//
// Critical path: 0→2→4→5 = 1+5+1 = 7

namespace {

    struct Empty {};
    struct WEdge { float w; };

    DAGStorage<Empty, WEdge> make_weighted_dag()
    {
        DAGBuilder<Empty, WEdge> b;
        for (int i = 0; i < 6; ++i) add_node(b, Empty{});
        add_edge(b, 0u, 1u, WEdge{ 3.f });
        add_edge(b, 0u, 2u, WEdge{ 1.f });
        add_edge(b, 1u, 3u, WEdge{ 4.f });
        add_edge(b, 1u, 4u, WEdge{ 2.f });
        add_edge(b, 2u, 4u, WEdge{ 5.f });
        add_edge(b, 3u, 5u, WEdge{ 2.f });
        add_edge(b, 4u, 5u, WEdge{ 1.f });
        auto result = build(std::move(b));
        assert(result.has_value());
        return std::move(*result);
    }

    // Polytree (see diagram):
    //
    //         0
    //        / \
    //       1   2
    //      / \   \
    //     3   4   5

    PolytreeStorage<Empty, Empty> make_polytree()
    {
        PolytreeBuilder<Empty, Empty> b;
        for (int i = 0; i < 6; ++i) add_node(b, Empty{});
        add_edge(b, 0u, 1u);
        add_edge(b, 0u, 2u);
        add_edge(b, 1u, 3u);
        add_edge(b, 1u, 4u);
        add_edge(b, 2u, 5u);
        auto result = build(std::move(b));
        assert(result.has_value());
        return std::move(*result);
    }

    auto weight_fn = [](NodeHandle, NodeHandle, const WEdge& e) { return e.w; };

} // namespace


// ─── Reachability — DAG ───────────────────────────────────────────────────────

TEST(GraphProblemsSpec, DAGReachabilityDirectEdge)
{
    auto storage = make_weighted_dag();
    EXPECT_TRUE(is_reachable(storage.dag, 0u, 1u));
    EXPECT_TRUE(is_reachable(storage.dag, 0u, 2u));
}

TEST(GraphProblemsSpec, DAGReachabilityTransitive)
{
    auto storage = make_weighted_dag();
    EXPECT_TRUE(is_reachable(storage.dag, 0u, 5u));
    EXPECT_TRUE(is_reachable(storage.dag, 1u, 5u));
    EXPECT_TRUE(is_reachable(storage.dag, 2u, 5u));
}

TEST(GraphProblemsSpec, DAGReachabilityNotReversible)
{
    auto storage = make_weighted_dag();
    EXPECT_FALSE(is_reachable(storage.dag, 5u, 0u));
    EXPECT_FALSE(is_reachable(storage.dag, 3u, 2u));
}

TEST(GraphProblemsSpec, DAGReachabilityUnrelated)
{
    auto storage = make_weighted_dag();
    // 3 and 2 are in different subtrees, neither reaches the other
    EXPECT_FALSE(is_reachable(storage.dag, 3u, 2u));
    EXPECT_FALSE(is_reachable(storage.dag, 2u, 3u));
}

TEST(GraphProblemsSpec, DAGReachabilityToSelf)
{
    auto storage = make_weighted_dag();
    EXPECT_TRUE(is_reachable(storage.dag, 3u, 3u));
}


// ─── Reachability — Polytree ──────────────────────────────────────────────────

TEST(GraphProblemsSpec, PolytreeReachabilityTransitive)
{
    auto storage = make_polytree();
    EXPECT_TRUE(is_reachable(storage.polytree, 0u, 3u));
    EXPECT_TRUE(is_reachable(storage.polytree, 0u, 5u));
    EXPECT_TRUE(is_reachable(storage.polytree, 1u, 4u));
}

TEST(GraphProblemsSpec, PolytreeReachabilityNotReversible)
{
    auto storage = make_polytree();
    EXPECT_FALSE(is_reachable(storage.polytree, 3u, 0u));
    EXPECT_FALSE(is_reachable(storage.polytree, 5u, 1u));
}

TEST(GraphProblemsSpec, PolytreeReachabilityAcrossBranches)
{
    auto storage = make_polytree();
    // 3 and 5 are in different subtrees — neither reaches the other
    EXPECT_FALSE(is_reachable(storage.polytree, 3u, 5u));
    EXPECT_FALSE(is_reachable(storage.polytree, 5u, 3u));
}


// ─── Critical path ────────────────────────────────────────────────────────────

TEST(GraphProblemsSpec, CriticalPathCostIsCorrect)
{
    auto storage = make_weighted_dag();
    auto result = critical_path(storage.dag, weight_fn);
    EXPECT_FLOAT_EQ(result.cost, 9.f); // 0→1→3→5 = 3+4+2
}

TEST(GraphProblemsSpec, CriticalPathStartsAtRoot)
{
    auto storage = make_weighted_dag();
    auto result = critical_path(storage.dag, weight_fn);
    ASSERT_FALSE(result.path.empty());
    EXPECT_EQ(result.path.front(), 0u);
}

TEST(GraphProblemsSpec, CriticalPathEndsAtLeaf)
{
    auto storage = make_weighted_dag();
    auto result = critical_path(storage.dag, weight_fn);
    ASSERT_FALSE(result.path.empty());
    EXPECT_TRUE(is_leaf(storage.dag, result.path.back()));
}

TEST(GraphProblemsSpec, CriticalPathSequenceIsCorrect)
{
    auto storage = make_weighted_dag();
    auto result = critical_path(storage.dag, weight_fn);

    ASSERT_EQ(result.path.size(), 4u);
    EXPECT_EQ(result.path[0], 0u);
    EXPECT_EQ(result.path[1], 1u);
    EXPECT_EQ(result.path[2], 3u);
    EXPECT_EQ(result.path[3], 5u);
}

TEST(GraphProblemsSpec, CriticalPathEachStepIsEdge)
{
    auto storage = make_weighted_dag();
    auto result = critical_path(storage.dag, weight_fn);

    for (uint32_t i = 0; i + 1 < result.path.size(); ++i)
        EXPECT_TRUE(has_edge(storage.dag, result.path[i], result.path[i + 1]))
        << "no edge between path[" << i << "] and path[" << i + 1 << "]";
}

TEST(GraphProblemsSpec, CriticalPathEmptyGraph)
{
    DAGBuilder<Empty, WEdge> b;
    auto storage = build(std::move(b));
    ASSERT_TRUE(storage.has_value());
    auto result = critical_path(storage->dag, weight_fn);
    EXPECT_EQ(result.path.size(), 0u);
    EXPECT_FLOAT_EQ(result.cost, 0.f);
}


// ─── Lowest common ancestor ───────────────────────────────────────────────────

TEST(GraphProblemsSpec, LCASiblings)
{
    auto storage = make_polytree();
    // 3 and 4 are siblings under 1
    auto lca = lowest_common_ancestor(storage.polytree, 3u, 4u);
    ASSERT_TRUE(lca.has_value());
    EXPECT_EQ(*lca, 1u);
}

TEST(GraphProblemsSpec, LCACousins)
{
    auto storage = make_polytree();
    // 3 is under 1, 5 is under 2 — LCA is root 0
    auto lca = lowest_common_ancestor(storage.polytree, 3u, 5u);
    ASSERT_TRUE(lca.has_value());
    EXPECT_EQ(*lca, 0u);
}

TEST(GraphProblemsSpec, LCAAcrossBranches)
{
    auto storage = make_polytree();
    // 4 is under 1, 5 is under 2 — LCA is root 0
    auto lca = lowest_common_ancestor(storage.polytree, 4u, 5u);
    ASSERT_TRUE(lca.has_value());
    EXPECT_EQ(*lca, 0u);
}

TEST(GraphProblemsSpec, LCANodeWithAncestor)
{
    auto storage = make_polytree();
    // LCA(1, 3) = 1 since 1 is an ancestor of 3
    auto lca = lowest_common_ancestor(storage.polytree, 3u, 1u);
    ASSERT_TRUE(lca.has_value());
    EXPECT_EQ(*lca, 1u);
}

TEST(GraphProblemsSpec, LCASameNode)
{
    auto storage = make_polytree();
    auto lca = lowest_common_ancestor(storage.polytree, 3u, 3u);
    ASSERT_TRUE(lca.has_value());
    EXPECT_EQ(*lca, 3u);
}


// ─── Depth assignment ─────────────────────────────────────────────────────────

TEST(GraphProblemsSpec, DAGDepthsAreCorrect)
{
    auto storage = make_weighted_dag();
    auto& g = storage.dag;

    uint32_t depths[6] = {};
    compute_depths(g, depths);

    EXPECT_EQ(depths[0], 0u); // root
    EXPECT_EQ(depths[1], 1u);
    EXPECT_EQ(depths[2], 1u);
    EXPECT_EQ(depths[3], 2u);
    EXPECT_EQ(depths[4], 2u); // reached via 0→2→4, depth 2
    EXPECT_EQ(depths[5], 3u); // leaf
}

TEST(GraphProblemsSpec, PolytreeDepthsAreCorrect)
{
    auto storage = make_polytree();
    auto& t = storage.polytree;

    uint32_t depths[6] = {};
    compute_depths(t, depths);

    EXPECT_EQ(depths[0], 0u);
    EXPECT_EQ(depths[1], 1u);
    EXPECT_EQ(depths[2], 1u);
    EXPECT_EQ(depths[3], 2u);
    EXPECT_EQ(depths[4], 2u);
    EXPECT_EQ(depths[5], 2u);
}

TEST(GraphProblemsSpec, RootAlwaysDepthZero)
{
    auto storage = make_weighted_dag();
    uint32_t depths[6] = {};
    compute_depths(storage.dag, depths);

    for (uint32_t i = 0; i < node_count(storage.dag); ++i)
        if (is_root(storage.dag, i))
            EXPECT_EQ(depths[i], 0u);
}