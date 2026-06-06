#include <gtest/gtest.h>
#include <graph/static_polytree.h>

using namespace wz::core::graph;

struct Transform { float x, y, z; };
struct Weight { float w; };

// ─── Fixtures ─────────────────────────────────────────────────────────────────
//
// Two topologies used throughout:
//
// Chain:  root --> mid --> leaf
//
// Wide:      root
//           / | \
//          a  b  c
//              |
//              d
//
// The diamond topology used in DAG tests is intentionally absent —
// it cannot be a polytree since sink has two parents.

namespace {

    struct Empty {};

    //  0 --> 1 --> 2
    PolytreeStorage<Transform, Weight> make_chain()
    {
        PolytreeBuilder<Transform, Weight> b;
        auto root = add_node(b, Transform{ 0,0,0 });
        auto mid = add_node(b, Transform{ 1,0,0 });
        auto leaf = add_node(b, Transform{ 2,0,0 });
        add_edge(b, root, mid, Weight{ 1.f });
        add_edge(b, mid, leaf, Weight{ 2.f });
        auto result = build(std::move(b));
        assert(result.has_value());
        return std::move(*result);
    }

    //       0
    //      /|\
    //     1 2 3
    //       |
    //       4
    PolytreeStorage<Empty, Empty> make_wide()
    {
        PolytreeBuilder<Empty, Empty> b;
        auto root = add_node(b, Empty{});  // 0
        auto a = add_node(b, Empty{});  // 1
        auto bnode = add_node(b, Empty{});  // 2
        auto c = add_node(b, Empty{});  // 3
        auto d = add_node(b, Empty{});  // 4
        add_edge(b, root, a);
        add_edge(b, root, bnode);
        add_edge(b, root, c);
        add_edge(b, bnode, d);
        auto result = build(std::move(b));
        assert(result.has_value());
        return std::move(*result);
    }

} // namespace


// ─── Builder ──────────────────────────────────────────────────────────────────

TEST(PolytreeSpec, AddNodeReturnsSequentialHandles)
{
    PolytreeBuilder<Empty, Empty> b;
    EXPECT_EQ(add_node(b, Empty{}), 0u);
    EXPECT_EQ(add_node(b, Empty{}), 1u);
    EXPECT_EQ(add_node(b, Empty{}), 2u);
}

TEST(PolytreeSpec, AddEdgeRejectsSelfLoop)
{
    PolytreeBuilder<Empty, Empty> b;
    auto a = add_node(b, Empty{});
    EXPECT_FALSE(add_edge(b, a, a));
}

TEST(PolytreeSpec, AddEdgeRejectsInvalidHandles)
{
    PolytreeBuilder<Empty, Empty> b;
    add_node(b, Empty{});
    EXPECT_FALSE(add_edge(b, 0u, 99u));
    EXPECT_FALSE(add_edge(b, 99u, 0u));
}

TEST(PolytreeSpec, AddEdgeRejectsSecondParent)
{
    PolytreeBuilder<Empty, Empty> b;
    auto a = add_node(b, Empty{});
    auto c = add_node(b, Empty{});
    auto d = add_node(b, Empty{});
    EXPECT_TRUE(add_edge(b, a, c));
    // c already has parent a — must be rejected
    EXPECT_FALSE(add_edge(b, d, c));
}


// ─── Build ────────────────────────────────────────────────────────────────────

TEST(PolytreeSpec, BuildSucceedsOnValidGraph)
{
    auto storage = make_chain();
    EXPECT_EQ(node_count(storage.polytree), 3u);
    EXPECT_EQ(edge_count(storage.polytree), 2u);
}

TEST(PolytreeSpec, BuildFailsOnCycle)
{
    PolytreeBuilder<Empty, Empty> b;
    auto a = add_node(b, Empty{});
    auto c = add_node(b, Empty{});
    add_edge(b, a, c);
    // force a cycle by bypassing the parent guard
    // (push directly — simulating corrupt builder state)
    b.edges.push_back({ c, a, Empty{} });

    auto result = build(std::move(b));
    EXPECT_FALSE(result.has_value());
}

TEST(PolytreeSpec, BuildSingleNode)
{
    PolytreeBuilder<Empty, Empty> b;
    add_node(b, Empty{});
    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(node_count(result->polytree), 1u);
    EXPECT_EQ(edge_count(result->polytree), 0u);
}

TEST(PolytreeSpec, NodeDataIsPreserved)
{
    auto storage = make_chain();
    auto& t = storage.polytree;

    EXPECT_FLOAT_EQ(node_data(t, 0u).x, 0.f);
    EXPECT_FLOAT_EQ(node_data(t, 1u).x, 1.f);
    EXPECT_FLOAT_EQ(node_data(t, 2u).x, 2.f);
}


// ─── Parent queries ───────────────────────────────────────────────────────────

TEST(PolytreeSpec, RootHasInvalidParent)
{
    auto storage = make_chain();
    EXPECT_EQ(parent(storage.polytree, 0u), INVALID_NODE);
}

TEST(PolytreeSpec, ParentLookupIsCorrect)
{
    auto storage = make_chain();
    auto& t = storage.polytree;

    EXPECT_EQ(parent(t, 1u), 0u);
    EXPECT_EQ(parent(t, 2u), 1u);
}

TEST(PolytreeSpec, ParentEdgeDataIsPreserved)
{
    auto storage = make_chain();
    auto& t = storage.polytree;

    EXPECT_FLOAT_EQ(parent_edge_data(t, 1u).w, 1.f);
    EXPECT_FLOAT_EQ(parent_edge_data(t, 2u).w, 2.f);
}

TEST(PolytreeSpec, IsRootIsLeafAreCorrect)
{
    auto storage = make_chain();
    auto& t = storage.polytree;

    EXPECT_TRUE(is_root(t, 0u));
    EXPECT_FALSE(is_root(t, 1u));
    EXPECT_FALSE(is_root(t, 2u));

    EXPECT_FALSE(is_leaf(t, 0u));
    EXPECT_FALSE(is_leaf(t, 1u));
    EXPECT_TRUE(is_leaf(t, 2u));
}


// ─── Children queries ─────────────────────────────────────────────────────────

TEST(PolytreeSpec, WideRootHasThreeChildren)
{
    auto storage = make_wide();
    auto ch = children(storage.polytree, 0u);
    ASSERT_EQ(ch.size(), 3u);
}

TEST(PolytreeSpec, ChildrenAreCorrect)
{
    auto storage = make_chain();
    auto& t = storage.polytree;

    auto ch0 = children(t, 0u);
    ASSERT_EQ(ch0.size(), 1u);
    EXPECT_EQ(ch0[0], 1u);

    auto ch1 = children(t, 1u);
    ASSERT_EQ(ch1.size(), 1u);
    EXPECT_EQ(ch1[0], 2u);

    EXPECT_EQ(children(t, 2u).size(), 0u);
}

TEST(PolytreeSpec, HasEdgeIsCorrect)
{
    auto storage = make_chain();
    auto& t = storage.polytree;

    EXPECT_TRUE(has_edge(t, 0u, 1u));
    EXPECT_TRUE(has_edge(t, 1u, 2u));
    EXPECT_FALSE(has_edge(t, 0u, 2u));
    EXPECT_FALSE(has_edge(t, 2u, 0u));
}


// ─── Topological order ────────────────────────────────────────────────────────

TEST(PolytreeSpec, TopoOrderVisitsParentBeforeChild)
{
    auto storage = make_chain();
    auto order = topo_order(storage.polytree);

    ASSERT_EQ(order.size(), 3u);

    auto pos = [&](NodeHandle h) {
        return std::find(order.begin(), order.end(), h) - order.begin();
        };

    EXPECT_LT(pos(0u), pos(1u));
    EXPECT_LT(pos(1u), pos(2u));
}

TEST(PolytreeSpec, TopoOrderWideRootIsFirst)
{
    auto storage = make_wide();
    auto order = topo_order(storage.polytree);

    ASSERT_EQ(order.size(), 5u);
    EXPECT_EQ(order[0], 0u);
}


// ─── Traversal ────────────────────────────────────────────────────────────────

TEST(PolytreeSpec, DFSVisitsAllNodes)
{
    auto storage = make_wide();
    auto& t = storage.polytree;

    std::vector<NodeHandle> visited;
    dfs(t, 0u, [&](NodeHandle n) { visited.push_back(n); });

    ASSERT_EQ(visited.size(), 5u);
    for (NodeHandle h : {0u, 1u, 2u, 3u, 4u})
        EXPECT_EQ(std::count(visited.begin(), visited.end(), h), 1)
        << "handle " << h << " not visited exactly once";
}

TEST(PolytreeSpec, BFSVisitsAllNodes)
{
    auto storage = make_wide();
    auto& t = storage.polytree;

    std::vector<NodeHandle> visited;
    bfs(t, 0u, [&](NodeHandle n) { visited.push_back(n); });

    ASSERT_EQ(visited.size(), 5u);
    for (NodeHandle h : {0u, 1u, 2u, 3u, 4u})
        EXPECT_EQ(std::count(visited.begin(), visited.end(), h), 1)
        << "handle " << h << " not visited exactly once";
}

TEST(PolytreeSpec, BFSVisitsInLevelOrder)
{
    auto storage = make_wide();
    std::vector<NodeHandle> visited;
    bfs(storage.polytree, 0u, [&](NodeHandle n) { visited.push_back(n); });

    auto pos = [&](NodeHandle h) {
        return std::find(visited.begin(), visited.end(), h) - visited.begin();
        };

    // root before all children
    EXPECT_LT(pos(0u), pos(1u));
    EXPECT_LT(pos(0u), pos(2u));
    EXPECT_LT(pos(0u), pos(3u));

    // b (2) before its child d (4)
    EXPECT_LT(pos(2u), pos(4u));
}


// ─── Ancestor walk — unique to polytree ───────────────────────────────────────

TEST(PolytreeSpec, WalkAncestorsFromLeaf)
{
    auto storage = make_chain();
    auto& t = storage.polytree;

    std::vector<NodeHandle> ancestors;
    walk_ancestors(t, 2u, [&](NodeHandle n) { ancestors.push_back(n); });

    ASSERT_EQ(ancestors.size(), 2u);
    EXPECT_EQ(ancestors[0], 1u); // immediate parent first
    EXPECT_EQ(ancestors[1], 0u); // then grandparent
}

TEST(PolytreeSpec, WalkAncestorsFromRootIsEmpty)
{
    auto storage = make_chain();

    std::vector<NodeHandle> ancestors;
    walk_ancestors(storage.polytree, 0u,
        [&](NodeHandle n) { ancestors.push_back(n); });

    EXPECT_TRUE(ancestors.empty());
}

TEST(PolytreeSpec, WalkAncestorsFromMid)
{
    auto storage = make_chain();

    std::vector<NodeHandle> ancestors;
    walk_ancestors(storage.polytree, 1u,
        [&](NodeHandle n) { ancestors.push_back(n); });

    ASSERT_EQ(ancestors.size(), 1u);
    EXPECT_EQ(ancestors[0], 0u);
}