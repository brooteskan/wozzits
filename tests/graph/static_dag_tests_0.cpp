#include <gtest/gtest.h>
#include <graph/static_dag.h>

using namespace wz::core::graph;

struct Transform { float x, y, z; };
struct Weight { float w; };

// ─── Builder ──────────────────────────────────────────────────────────────────

TEST(DAGSpec, AddNodeReturnsSequentialHandles)
{
    DAGBuilder<Transform, Weight> b;
    auto a = add_node(b, Transform{ 0,0,0 });
    auto c = add_node(b, Transform{ 1,0,0 });
    auto d = add_node(b, Transform{ 2,0,0 });

    EXPECT_EQ(a, 0u);
    EXPECT_EQ(c, 1u);
    EXPECT_EQ(d, 2u);
}

TEST(DAGSpec, AddEdgeRejectsSelfLoop)
{
    DAGBuilder<Transform, Weight> b;
    auto a = add_node(b, Transform{});

    EXPECT_FALSE(add_edge(b, a, a));
}

TEST(DAGSpec, AddEdgeRejectsInvalidHandles)
{
    DAGBuilder<Transform, Weight> b;
    add_node(b, Transform{});

    EXPECT_FALSE(add_edge(b, 0u, 99u));
    EXPECT_FALSE(add_edge(b, 99u, 0u));
}

// ─── Build ────────────────────────────────────────────────────────────────────

TEST(DAGSpec, BuildSucceedsOnValidGraph)
{
    DAGBuilder<Transform, Weight> b;
    auto root = add_node(b, Transform{ 0,0,0 });
    auto child = add_node(b, Transform{ 1,0,0 });
    add_edge(b, root, child);

    auto result = build(std::move(b));
    EXPECT_TRUE(result.has_value());
}

TEST(DAGSpec, BuildFailsOnCycle)
{
    DAGBuilder<Transform, Weight> b;
    auto a = add_node(b, Transform{});
    auto c = add_node(b, Transform{});
    add_edge(b, a, c);
    add_edge(b, c, a);

    auto result = build(std::move(b));
    EXPECT_FALSE(result.has_value());
}

TEST(DAGSpec, BuildPreservesNodeCount)
{
    DAGBuilder<Transform, Weight> b;
    add_node(b, Transform{ 0,0,0 });
    add_node(b, Transform{ 1,0,0 });
    add_node(b, Transform{ 2,0,0 });

    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(node_count(result->dag), 3u);
}

TEST(DAGSpec, BuildPreservesEdgeCount)
{
    DAGBuilder<Transform, Weight> b;
    auto root = add_node(b, Transform{});
    auto left = add_node(b, Transform{});
    auto right = add_node(b, Transform{});
    add_edge(b, root, left);
    add_edge(b, root, right);

    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(edge_count(result->dag), 2u);
}

// ─── Queries ──────────────────────────────────────────────────────────────────

TEST(DAGSpec, ChildrenAndParentsAreConsistent)
{
    DAGBuilder<Transform, Weight> b;
    auto root = add_node(b, Transform{});
    auto child = add_node(b, Transform{});
    add_edge(b, root, child);

    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());
    auto& g = result->dag;

    auto ch = children(g, root);
    ASSERT_EQ(ch.size(), 1u);
    EXPECT_EQ(ch[0], child);

    auto pa = parents(g, child);
    ASSERT_EQ(pa.size(), 1u);
    EXPECT_EQ(pa[0], root);
}

TEST(DAGSpec, RootHasNoParents)
{
    DAGBuilder<Transform, Weight> b;
    auto root = add_node(b, Transform{});
    auto child = add_node(b, Transform{});
    add_edge(b, root, child);

    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());

    EXPECT_TRUE(is_root(result->dag, root));
    EXPECT_FALSE(is_root(result->dag, child));
}

TEST(DAGSpec, LeafHasNoChildren)
{
    DAGBuilder<Transform, Weight> b;
    auto root = add_node(b, Transform{});
    auto child = add_node(b, Transform{});
    add_edge(b, root, child);

    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());

    EXPECT_TRUE(is_leaf(result->dag, child));
    EXPECT_FALSE(is_leaf(result->dag, root));
}

TEST(DAGSpec, NodeDataIsPreserved)
{
    DAGBuilder<Transform, Weight> b;
    add_node(b, Transform{ 1.f, 2.f, 3.f });

    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());

    auto& t = node_data(result->dag, 0u);
    EXPECT_FLOAT_EQ(t.x, 1.f);
    EXPECT_FLOAT_EQ(t.y, 2.f);
    EXPECT_FLOAT_EQ(t.z, 3.f);
}

// ─── Topological order ────────────────────────────────────────────────────────

TEST(DAGSpec, TopoOrderVisitsParentBeforeChild)
{
    DAGBuilder<Transform, Weight> b;
    auto root = add_node(b, Transform{});
    auto mid = add_node(b, Transform{});
    auto leaf = add_node(b, Transform{});
    add_edge(b, root, mid);
    add_edge(b, mid, leaf);

    auto result = build(std::move(b));
    ASSERT_TRUE(result.has_value());

    auto order = topo_order(result->dag);
    ASSERT_EQ(order.size(), 3u);

    auto pos = [&](NodeHandle h) {
        return std::find(order.begin(), order.end(), h) - order.begin();
        };

    EXPECT_LT(pos(root), pos(mid));
    EXPECT_LT(pos(mid), pos(leaf));
}

// ─── Traversal ────────────────────────────────────────────────────────────────
//
// Diamond topology used throughout:
//
//      root
//      /  \
//    left  right
//      \  /
//      sink
//
// This is the simplest DAG that cannot be a polytree (sink has two parents).
// It exercises the visited-guard that prevents double-visitation.

namespace {

    struct Empty {};

    // Builds the diamond and returns DAGStorage.
    // root=0, left=1, right=2, sink=3
    DAGStorage<Empty, Empty> make_diamond()
    {
        DAGBuilder<Empty, Empty> b;
        auto root = add_node(b, Empty{});  // 0
        auto left = add_node(b, Empty{});  // 1
        auto right = add_node(b, Empty{});  // 2
        auto sink = add_node(b, Empty{});  // 3
        add_edge(b, root, left);
        add_edge(b, root, right);
        add_edge(b, left, sink);
        add_edge(b, right, sink);
        auto result = build(std::move(b));
        assert(result.has_value());
        return std::move(*result);
    }

    DAGStorage<Empty, Empty> make_chain(int length)
    {
        DAGBuilder<Empty, Empty> b;
        NodeHandle prev = add_node(b, Empty{});
        for (int i = 1; i < length; ++i) {
            NodeHandle curr = add_node(b, Empty{});
            add_edge(b, prev, curr);
            prev = curr;
        }
        auto result = build(std::move(b));
        assert(result.has_value());
        return std::move(*result);
    }

} // namespace


// ─── DFS ──────────────────────────────────────────────────────────────────────

TEST(DAGTraversalSpec, DFSVisitsAllNodes)
{
    auto storage = make_diamond();
    auto& g = storage.dag;

    std::vector<NodeHandle> visited;
    dfs(g, 0u, [&](NodeHandle n) { visited.push_back(n); });

    ASSERT_EQ(visited.size(), 4u);

    // Every handle appears exactly once
    for (NodeHandle h : {0u, 1u, 2u, 3u}) {
        EXPECT_EQ(std::count(visited.begin(), visited.end(), h), 1)
            << "handle " << h << " not visited exactly once";
    }
}

TEST(DAGTraversalSpec, DFSVisitsRootFirst)
{
    auto storage = make_diamond();
    std::vector<NodeHandle> visited;
    dfs(storage.dag, 0u, [&](NodeHandle n) { visited.push_back(n); });

    EXPECT_EQ(visited.front(), 0u);
}

TEST(DAGTraversalSpec, DFSSinkVisitedOnce)
{
    auto storage = make_diamond();
    std::vector<NodeHandle> visited;
    dfs(storage.dag, 0u, [&](NodeHandle n) { visited.push_back(n); });

    EXPECT_EQ(std::count(visited.begin(), visited.end(), 3u), 1)
        << "sink (two parents) must not be visited twice";
}

TEST(DAGTraversalSpec, DFSVisitsParentBeforeChildOnChain)
{
    auto storage = make_chain(4);
    auto& g = storage.dag;

    std::vector<NodeHandle> visited;
    dfs(g, 0u, [&](NodeHandle n) { visited.push_back(n); });

    ASSERT_EQ(visited.size(), 4u);

    auto pos = [&](NodeHandle h) {
        return std::find(visited.begin(), visited.end(), h) - visited.begin();
        };

    EXPECT_LT(pos(0u), pos(1u));
    EXPECT_LT(pos(1u), pos(2u));
    EXPECT_LT(pos(2u), pos(3u));
}

TEST(DAGTraversalSpec, DFSOnSingleNode)
{
    DAGBuilder<Empty, Empty> b;
    add_node(b, Empty{});
    auto storage = build(std::move(b));
    ASSERT_TRUE(storage.has_value());

    std::vector<NodeHandle> visited;
    dfs(storage->dag, 0u, [&](NodeHandle n) { visited.push_back(n); });

    ASSERT_EQ(visited.size(), 1u);
    EXPECT_EQ(visited[0], 0u);
}


// ─── BFS ──────────────────────────────────────────────────────────────────────

TEST(DAGTraversalSpec, BFSVisitsAllNodes)
{
    auto storage = make_diamond();
    auto& g = storage.dag;

    std::vector<NodeHandle> visited;
    bfs(g, 0u, [&](NodeHandle n) { visited.push_back(n); });

    ASSERT_EQ(visited.size(), 4u);

    for (NodeHandle h : {0u, 1u, 2u, 3u}) {
        EXPECT_EQ(std::count(visited.begin(), visited.end(), h), 1)
            << "handle " << h << " not visited exactly once";
    }
}

TEST(DAGTraversalSpec, BFSVisitsRootFirst)
{
    auto storage = make_diamond();
    std::vector<NodeHandle> visited;
    bfs(storage.dag, 0u, [&](NodeHandle n) { visited.push_back(n); });

    EXPECT_EQ(visited.front(), 0u);
}

TEST(DAGTraversalSpec, BFSSinkVisitedOnce)
{
    auto storage = make_diamond();
    std::vector<NodeHandle> visited;
    bfs(storage.dag, 0u, [&](NodeHandle n) { visited.push_back(n); });

    EXPECT_EQ(std::count(visited.begin(), visited.end(), 3u), 1)
        << "sink (two parents) must not be visited twice";
}

TEST(DAGTraversalSpec, BFSVisitsInLevelOrder)
{
    auto storage = make_diamond();
    auto& g = storage.dag;

    std::vector<NodeHandle> visited;
    bfs(g, 0u, [&](NodeHandle n) { visited.push_back(n); });

    auto pos = [&](NodeHandle h) {
        return std::find(visited.begin(), visited.end(), h) - visited.begin();
        };

    // root before both children
    EXPECT_LT(pos(0u), pos(1u));
    EXPECT_LT(pos(0u), pos(2u));

    // both children before sink
    EXPECT_LT(pos(1u), pos(3u));
    EXPECT_LT(pos(2u), pos(3u));
}

TEST(DAGTraversalSpec, BFSOnSingleNode)
{
    DAGBuilder<Empty, Empty> b;
    add_node(b, Empty{});
    auto storage = build(std::move(b));
    ASSERT_TRUE(storage.has_value());

    std::vector<NodeHandle> visited;
    bfs(storage->dag, 0u, [&](NodeHandle n) { visited.push_back(n); });

    ASSERT_EQ(visited.size(), 1u);
    EXPECT_EQ(visited[0], 0u);
}

// ─── Dynamic subgraph as node payload ────────────────────────────────────────
//
// Outer graph: a static DAG of "zones" (e.g. areas of a level).
// Inner graph: each zone node carries its own static DAG of "agents"
//              occupying that zone.
//
// Outer topology:
//
//   zone_a --> zone_b --> zone_c
//
// Inner topologies (agent dependency graphs per zone):
//
//   zone_a:  scout --> captain
//   zone_b:  scout (lone agent)
//   zone_c:  (empty — unoccupied zone)
//
// This validates:
//   - DAGStorage as a move-only NodeData type survives build()
//   - Inner graphs are accessible and correct after outer build()
//   - An empty inner graph is a valid payload

namespace {

    struct Agent { const char* role; };
    struct ZoneEdge {};
    struct AgentEdge {};

    using AgentGraph = DAGStorage<Agent, AgentEdge>;
    using ZoneGraph = DAGStorage<AgentGraph, ZoneEdge>;

    AgentGraph make_agent_graph_two()
    {
        DAGBuilder<Agent, AgentEdge> b;
        auto scout = add_node(b, Agent{ "scout" });
        auto captain = add_node(b, Agent{ "captain" });
        add_edge(b, scout, captain);
        auto result = build(std::move(b));
        assert(result.has_value());
        return std::move(*result);
    }

    AgentGraph make_agent_graph_one()
    {
        DAGBuilder<Agent, AgentEdge> b;
        add_node(b, Agent{ "scout" });
        auto result = build(std::move(b));
        assert(result.has_value());
        return std::move(*result);
    }

    AgentGraph make_agent_graph_empty()
    {
        DAGBuilder<Agent, AgentEdge> b;
        auto result = build(std::move(b));
        assert(result.has_value());
        return std::move(*result);
    }

    ZoneGraph make_zone_graph()
    {
        DAGBuilder<AgentGraph, ZoneEdge> b;

        auto zone_a = add_node(b, make_agent_graph_two());
        auto zone_b = add_node(b, make_agent_graph_one());
        auto zone_c = add_node(b, make_agent_graph_empty());

        add_edge(b, zone_a, zone_b);
        add_edge(b, zone_b, zone_c);

        auto result = build(std::move(b));
        assert(result.has_value());
        return std::move(*result);
    }

} // namespace


// ─── Outer graph structure ────────────────────────────────────────────────────

TEST(DAGCompositionSpec, OuterGraphHasCorrectNodeCount)
{
    auto storage = make_zone_graph();
    EXPECT_EQ(node_count(storage.dag), 3u);
}

TEST(DAGCompositionSpec, OuterGraphHasCorrectEdgeCount)
{
    auto storage = make_zone_graph();
    EXPECT_EQ(edge_count(storage.dag), 2u);
}

TEST(DAGCompositionSpec, OuterGraphTopologyIsCorrect)
{
    auto storage = make_zone_graph();
    auto& g = storage.dag;

    auto ch_a = children(g, 0u);
    ASSERT_EQ(ch_a.size(), 1u);
    EXPECT_EQ(ch_a[0], 1u);

    auto ch_b = children(g, 1u);
    ASSERT_EQ(ch_b.size(), 1u);
    EXPECT_EQ(ch_b[0], 2u);

    EXPECT_TRUE(is_leaf(g, 2u));
}


// ─── Inner graph access ───────────────────────────────────────────────────────

TEST(DAGCompositionSpec, InnerGraphAgentCountsAreCorrect)
{
    auto storage = make_zone_graph();
    auto& g = storage.dag;

    EXPECT_EQ(node_count(node_data(g, 0u).dag), 2u); // zone_a: scout + captain
    EXPECT_EQ(node_count(node_data(g, 1u).dag), 1u); // zone_b: scout
    EXPECT_EQ(node_count(node_data(g, 2u).dag), 0u); // zone_c: empty
}

TEST(DAGCompositionSpec, InnerGraphAgentDataIsPreserved)
{
    auto storage = make_zone_graph();
    auto& g = storage.dag;

    auto& zone_a_dag = node_data(g, 0u).dag;
    EXPECT_STREQ(node_data(zone_a_dag, 0u).role, "scout");
    EXPECT_STREQ(node_data(zone_a_dag, 1u).role, "captain");

    auto& zone_b_dag = node_data(g, 1u).dag;
    EXPECT_STREQ(node_data(zone_b_dag, 0u).role, "scout");
}

TEST(DAGCompositionSpec, InnerGraphTopologyIsCorrect)
{
    auto storage = make_zone_graph();
    auto& zone_a_dag = node_data(storage.dag, 0u).dag;

    // scout --> captain
    auto ch = children(zone_a_dag, 0u);
    ASSERT_EQ(ch.size(), 1u);
    EXPECT_EQ(ch[0], 1u);

    EXPECT_TRUE(is_root(zone_a_dag, 0u));
    EXPECT_TRUE(is_leaf(zone_a_dag, 1u));
}

TEST(DAGCompositionSpec, EmptyInnerGraphIsValid)
{
    auto storage = make_zone_graph();
    auto& zone_c_dag = node_data(storage.dag, 2u).dag;

    EXPECT_EQ(node_count(zone_c_dag), 0u);
    EXPECT_EQ(edge_count(zone_c_dag), 0u);
}


// ─── Traversal over outer graph reaching inner graphs ─────────────────────────

TEST(DAGCompositionSpec, BFSOverOuterVisitsAllInnerAgents)
{
    auto storage = make_zone_graph();
    auto& g = storage.dag;

    uint32_t total_agents = 0;
    bfs(g, 0u, [&](NodeHandle zone) {
        total_agents += node_count(node_data(g, zone).dag);
        });

    EXPECT_EQ(total_agents, 3u); // scout + captain + scout
}