#include <gtest/gtest.h>
#include <graph/shared_edge_graph.h>

#include <algorithm>
#include <set>
#include <vector>

using namespace wz::core::graph;

namespace {

    struct Belief { float v; };  // node payload
    struct Msg { int m; };       // edge payload (shared message slot)

    // Collect the neighbor set of node n into a sorted vector.
    template<typename G>
    std::vector<NodeHandle> neighbors_of(G& g, NodeHandle n) {
        std::vector<NodeHandle> out;
        for_each_neighbor(g, n, [&](NodeHandle nb, Msg&) { out.push_back(nb); });
        std::sort(out.begin(), out.end());
        return out;
    }

    // Triangle: 3 nodes, 3 edges (0-1, 1-2, 2-0). Every node degree 2.
    SharedEdgeGraph<Belief, Msg> make_triangle() {
        SharedEdgeGraphBuilder<Belief, Msg> b;
        auto n0 = add_node(b, Belief{ 0.f });
        auto n1 = add_node(b, Belief{ 1.f });
        auto n2 = add_node(b, Belief{ 2.f });
        EXPECT_TRUE(add_edge(b, n0, n1, Msg{ 10 }));
        EXPECT_TRUE(add_edge(b, n1, n2, Msg{ 20 }));
        EXPECT_TRUE(add_edge(b, n2, n0, Msg{ 30 }));
        return build(std::move(b));
    }

    // 4-cycle: 0-1, 1-2, 2-3, 3-0. Every node degree 2.
    SharedEdgeGraph<Belief, Msg> make_four_cycle() {
        SharedEdgeGraphBuilder<Belief, Msg> b;
        auto n0 = add_node(b, Belief{ 0.f });
        auto n1 = add_node(b, Belief{ 1.f });
        auto n2 = add_node(b, Belief{ 2.f });
        auto n3 = add_node(b, Belief{ 3.f });
        EXPECT_TRUE(add_edge(b, n0, n1, Msg{ 1 }));
        EXPECT_TRUE(add_edge(b, n1, n2, Msg{ 2 }));
        EXPECT_TRUE(add_edge(b, n2, n3, Msg{ 3 }));
        EXPECT_TRUE(add_edge(b, n3, n0, Msg{ 4 }));
        return build(std::move(b));
    }

} // namespace


// 1. Triangle counts and degrees.
TEST(SharedEdgeGraph, TriangleCountsAndDegrees) {
    auto g = make_triangle();
    EXPECT_EQ(node_count(g), 3u);
    EXPECT_EQ(edge_count(g), 3u);
    for (NodeHandle n = 0; n < 3; ++n)
        EXPECT_EQ(degree(g, n), 2u) << "triangle node " << n;
}

// 2. 4-cycle counts and degrees.
TEST(SharedEdgeGraph, FourCycleCountsAndDegrees) {
    auto g = make_four_cycle();
    EXPECT_EQ(node_count(g), 4u);
    EXPECT_EQ(edge_count(g), 4u);
    for (NodeHandle n = 0; n < 4; ++n)
        EXPECT_EQ(degree(g, n), 2u) << "4-cycle node " << n;
}

// 3. for_each_neighbor visits exactly the correct neighbor set.
TEST(SharedEdgeGraph, NeighborSets) {
    auto g = make_triangle();
    EXPECT_EQ(neighbors_of(g, 0), (std::vector<NodeHandle>{ 1, 2 }));
    EXPECT_EQ(neighbors_of(g, 1), (std::vector<NodeHandle>{ 0, 2 }));
    EXPECT_EQ(neighbors_of(g, 2), (std::vector<NodeHandle>{ 0, 1 }));

    auto q = make_four_cycle();
    EXPECT_EQ(neighbors_of(q, 0), (std::vector<NodeHandle>{ 1, 3 }));
    EXPECT_EQ(neighbors_of(q, 1), (std::vector<NodeHandle>{ 0, 2 }));
    EXPECT_EQ(neighbors_of(q, 2), (std::vector<NodeHandle>{ 1, 3 }));
    EXPECT_EQ(neighbors_of(q, 3), (std::vector<NodeHandle>{ 0, 2 }));
}

// 4. SHARED SLOT — the key property.
// Mutate an edge's payload via ONE endpoint, read it via the OTHER, and confirm
// both incidences resolve to the same edge_store slot (same value AND address).
TEST(SharedEdgeGraph, SharedEdgeSingleStorage) {
    auto g = make_triangle();

    // Edge 0-1: write the payload while visiting neighbor 1 from node 0.
    for_each_neighbor(g, 0, [&](NodeHandle nb, Msg& e) {
        if (nb == 1) e.m = 42;
        });
    // Read it back while visiting neighbor 0 from node 1.
    int read_back = 0;
    for_each_neighbor(g, 1, [&](NodeHandle nb, Msg& e) {
        if (nb == 0) read_back = e.m;
        });
    EXPECT_EQ(read_back, 42);

    // Same address from both endpoints — literally one object.
    Msg* from_zero = nullptr;
    for_each_neighbor(g, 0, [&](NodeHandle nb, Msg& e) {
        if (nb == 1) from_zero = &e;
        });
    Msg* from_one = nullptr;
    for_each_neighbor(g, 1, [&](NodeHandle nb, Msg& e) {
        if (nb == 0) from_one = &e;
        });
    EXPECT_NE(from_zero, nullptr);
    EXPECT_EQ(from_zero, from_one);

    // Mutate through the other endpoint, observe from the first.
    for_each_neighbor(g, 1, [&](NodeHandle nb, Msg& e) {
        if (nb == 0) e.m = -7;
        });
    int reread = 0;
    for_each_neighbor(g, 0, [&](NodeHandle nb, Msg& e) {
        if (nb == 1) reread = e.m;
        });
    EXPECT_EQ(reread, -7);
}

// 5. Star — hub with N spokes.
TEST(SharedEdgeGraph, Star) {
    constexpr int N = 5;
    SharedEdgeGraphBuilder<Belief, Msg> b;
    auto hub = add_node(b, Belief{ 0.f });
    std::vector<NodeHandle> leaves;
    for (int i = 0; i < N; ++i) leaves.push_back(add_node(b, Belief{ float(i + 1) }));
    for (int i = 0; i < N; ++i)
        EXPECT_TRUE(add_edge(b, hub, leaves[i], Msg{ i }));
    auto g = build(std::move(b));

    EXPECT_EQ(node_count(g), uint32_t(N + 1));
    EXPECT_EQ(edge_count(g), uint32_t(N));
    EXPECT_EQ(degree(g, hub), uint32_t(N));           // hub sees every spoke
    for (auto leaf : leaves) EXPECT_EQ(degree(g, leaf), 1u);

    // hub's neighbor set is exactly the leaves
    std::vector<NodeHandle> expected(leaves.begin(), leaves.end());
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(neighbors_of(g, hub), expected);
    // each leaf's only neighbor is the hub
    for (auto leaf : leaves)
        EXPECT_EQ(neighbors_of(g, leaf), (std::vector<NodeHandle>{ hub }));
}

// 6. Disconnected graph — two components, total edge_count, per-component iteration.
TEST(SharedEdgeGraph, DisconnectedComponents) {
    SharedEdgeGraphBuilder<Belief, Msg> b;
    // Component A: triangle 0-1-2.
    auto a0 = add_node(b, Belief{ 0.f });
    auto a1 = add_node(b, Belief{ 1.f });
    auto a2 = add_node(b, Belief{ 2.f });
    // Component B: single edge 3-4.
    auto b0 = add_node(b, Belief{ 3.f });
    auto b1 = add_node(b, Belief{ 4.f });
    EXPECT_TRUE(add_edge(b, a0, a1, Msg{ 1 }));
    EXPECT_TRUE(add_edge(b, a1, a2, Msg{ 2 }));
    EXPECT_TRUE(add_edge(b, a2, a0, Msg{ 3 }));
    EXPECT_TRUE(add_edge(b, b0, b1, Msg{ 4 }));
    auto g = build(std::move(b));

    EXPECT_EQ(node_count(g), 5u);
    EXPECT_EQ(edge_count(g), 4u);           // total across both components

    // Component A: each triangle node degree 2, correct neighbor sets.
    EXPECT_EQ(neighbors_of(g, a0), (std::vector<NodeHandle>{ a1, a2 }));
    EXPECT_EQ(neighbors_of(g, a1), (std::vector<NodeHandle>{ a0, a2 }));
    EXPECT_EQ(neighbors_of(g, a2), (std::vector<NodeHandle>{ a0, a1 }));

    // Component B: the two nodes see only each other.
    EXPECT_EQ(degree(g, b0), 1u);
    EXPECT_EQ(degree(g, b1), 1u);
    EXPECT_EQ(neighbors_of(g, b0), (std::vector<NodeHandle>{ b1 }));
    EXPECT_EQ(neighbors_of(g, b1), (std::vector<NodeHandle>{ b0 }));
}

// 7. add_edge rejects self-loops and out-of-range handles.
TEST(SharedEdgeGraph, AddEdgeRejects) {
    SharedEdgeGraphBuilder<Belief, Msg> b;
    auto n0 = add_node(b, Belief{ 0.f });
    auto n1 = add_node(b, Belief{ 1.f });
    EXPECT_FALSE(add_edge(b, n0, n0, Msg{}));   // self-loop
    EXPECT_FALSE(add_edge(b, n0, 99, Msg{}));   // out of range (b)
    EXPECT_FALSE(add_edge(b, 99, n1, Msg{}));   // out of range (a)
    EXPECT_TRUE(add_edge(b, n0, n1, Msg{ 7 })); // valid
    auto g = build(std::move(b));
    EXPECT_EQ(edge_count(g), 1u);               // only the valid one landed
}

// 8. Parallel edges — the same pair added twice yields two stored edges,
// and the pair appears twice in each endpoint's neighbor list.
TEST(SharedEdgeGraph, ParallelEdges) {
    SharedEdgeGraphBuilder<Belief, Msg> b;
    auto n0 = add_node(b, Belief{ 0.f });
    auto n1 = add_node(b, Belief{ 1.f });
    EXPECT_TRUE(add_edge(b, n0, n1, Msg{ 100 }));
    EXPECT_TRUE(add_edge(b, n0, n1, Msg{ 200 }));  // parallel — allowed
    auto g = build(std::move(b));

    EXPECT_EQ(edge_count(g), 2u);
    EXPECT_EQ(degree(g, n0), 2u);
    EXPECT_EQ(degree(g, n1), 2u);

    // The neighbor appears twice in each endpoint's list.
    EXPECT_EQ(neighbors_of(g, n0), (std::vector<NodeHandle>{ n1, n1 }));
    EXPECT_EQ(neighbors_of(g, n1), (std::vector<NodeHandle>{ n0, n0 }));

    // Both distinct payloads survive; each parallel edge is its own shared slot.
    std::set<int> payloads;
    for_each_neighbor(g, n0, [&](NodeHandle, Msg& e) { payloads.insert(e.m); });
    EXPECT_EQ(payloads, (std::set<int>{ 100, 200 }));
}

// 9. node_data mutation round-trips through the type (incl. const overload).
TEST(SharedEdgeGraph, NodeDataRoundTrip) {
    auto g = make_triangle();
    EXPECT_FLOAT_EQ(node_data(g, 1).v, 1.f);
    node_data(g, 1).v = 99.f;
    EXPECT_FLOAT_EQ(node_data(g, 1).v, 99.f);

    const auto& cg = g;
    EXPECT_FLOAT_EQ(node_data(cg, 1).v, 99.f);  // const overload sees the write
}
