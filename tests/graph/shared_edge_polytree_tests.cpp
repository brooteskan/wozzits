#include <gtest/gtest.h>
#include <graph/shared_edge_polytree.h>

#include <algorithm>
#include <set>
#include <vector>

using namespace wz::core::graph;

namespace {

    struct Belief { float v; };  // node payload
    struct Msg { float m; };     // edge payload (shared message slot)

    // Standard fixture:
    //
    //     r (root)
    //    / \
    //   a   b
    //   |
    //   c
    //
    // Node ids: r=0, a=1, b=2, c=3.
    SharedEdgePolytree<Belief, Msg> make_tree() {
        SharedEdgePolytreeBuilder<Belief, Msg> b;
        auto r = add_node(b, Belief{ 0.f });
        auto a = add_node(b, Belief{ 1.f });
        auto bb = add_node(b, Belief{ 2.f });
        auto c = add_node(b, Belief{ 3.f });
        EXPECT_TRUE(add_edge(b, r, a, Msg{ 10.f }));
        EXPECT_TRUE(add_edge(b, r, bb, Msg{ 20.f }));
        EXPECT_TRUE(add_edge(b, a, c, Msg{ 30.f }));
        auto t = build(std::move(b));
        EXPECT_TRUE(t.has_value());
        return std::move(*t);
    }

    constexpr NodeHandle R = 0, A = 1, B = 2, C = 3;

} // namespace


// 1. node_data round-trips and is mutable.
TEST(SharedEdgePolytree, NodeDataRoundTripAndMutable) {
    auto t = make_tree();
    EXPECT_FLOAT_EQ(node_data(t, A).v, 1.f);

    node_data(t, A).v = 99.f;
    EXPECT_FLOAT_EQ(node_data(t, A).v, 99.f);

    const auto& ct = t;
    EXPECT_FLOAT_EQ(node_data(ct, A).v, 99.f);  // const overload sees the write
}

// 2. Shared edge — the critical property: same storage from both endpoints.
TEST(SharedEdgePolytree, SharedEdgeSingleStorage) {
    auto t = make_tree();

    // Write via the parent's view of the edge to child C.
    edge_to_child(t, A, C).m = 42.f;
    // Read via the child's view of the edge to its parent.
    EXPECT_FLOAT_EQ(edge_to_parent(t, C).m, 42.f);

    // They are literally the same object.
    EXPECT_EQ(&edge_to_child(t, A, C), &edge_to_parent(t, C));

    // Mutate via one accessor, observe via the other.
    edge_to_parent(t, C).m = -7.f;
    EXPECT_FLOAT_EQ(edge_to_child(t, A, C).m, -7.f);
}

// 3. Mutable payload persists through the type.
TEST(SharedEdgePolytree, MutablePayloadPersists) {
    auto t = make_tree();
    node_data(t, B).v = 123.f;
    edge_to_parent(t, B).m = 456.f;
    EXPECT_FLOAT_EQ(node_data(t, B).v, 123.f);
    EXPECT_FLOAT_EQ(edge_to_child(t, R, B).m, 456.f);
}

// 4. for_each_neighbor visits parent + all children.
TEST(SharedEdgePolytree, ForEachNeighborSet) {
    auto t = make_tree();

    // a has parent r and one child c -> {r, c}
    std::set<NodeHandle> visited;
    for_each_neighbor(t, A, [&](NodeHandle nb, Msg&) { visited.insert(nb); });
    EXPECT_EQ(visited, (std::set<NodeHandle>{ R, C }));

    // root r has only children {a, b}
    visited.clear();
    for_each_neighbor(t, R, [&](NodeHandle nb, Msg&) { visited.insert(nb); });
    EXPECT_EQ(visited, (std::set<NodeHandle>{ A, B }));

    // leaf c has only its parent {a}
    visited.clear();
    for_each_neighbor(t, C, [&](NodeHandle nb, Msg&) { visited.insert(nb); });
    EXPECT_EQ(visited, (std::set<NodeHandle>{ A }));

    // count check: a node with parent + k children -> 1 + k visits
    int count = 0;
    for_each_neighbor(t, A, [&](NodeHandle, Msg&) { ++count; });
    EXPECT_EQ(count, 1 + static_cast<int>(child_count(t, A)));
}

// 5. for_each_neighbor_except skips the excluded handle.
TEST(SharedEdgePolytree, ForEachNeighborExcept) {
    auto t = make_tree();

    // a's neighbors are {r, c}; exclude r -> {c}
    std::set<NodeHandle> visited;
    for_each_neighbor_except(t, A, R, [&](NodeHandle nb, Msg&) { visited.insert(nb); });
    EXPECT_EQ(visited, (std::set<NodeHandle>{ C }));

    // exclude c -> {r}
    visited.clear();
    for_each_neighbor_except(t, A, C, [&](NodeHandle nb, Msg&) { visited.insert(nb); });
    EXPECT_EQ(visited, (std::set<NodeHandle>{ R }));

    // root excluding one child -> the other child only
    visited.clear();
    for_each_neighbor_except(t, R, A, [&](NodeHandle nb, Msg&) { visited.insert(nb); });
    EXPECT_EQ(visited, (std::set<NodeHandle>{ B }));
}

// 6. Neighbor visit yields the SHARED edge.
TEST(SharedEdgePolytree, NeighborVisitYieldsSharedEdge) {
    auto t = make_tree();

    // Write through A's visit of neighbor C.
    for_each_neighbor(t, A, [&](NodeHandle nb, Msg& e) {
        if (nb == C) e.m = 777.f;
        });
    // Read through C's visit of neighbor A.
    float read_back = 0.f;
    for_each_neighbor(t, C, [&](NodeHandle nb, Msg& e) {
        if (nb == A) read_back = e.m;
        });
    EXPECT_FLOAT_EQ(read_back, 777.f);
}

// 7. Forest — two disjoint trees in one structure.
TEST(SharedEdgePolytree, Forest) {
    SharedEdgePolytreeBuilder<Belief, Msg> b;
    auto r0 = add_node(b, Belief{ 0.f });   // 0
    auto x0 = add_node(b, Belief{ 1.f });   // 1
    auto r1 = add_node(b, Belief{ 2.f });   // 2
    auto x1 = add_node(b, Belief{ 3.f });   // 3
    EXPECT_TRUE(add_edge(b, r0, x0, Msg{ 1.f }));
    EXPECT_TRUE(add_edge(b, r1, x1, Msg{ 2.f }));
    auto opt = build(std::move(b));
    ASSERT_TRUE(opt.has_value());
    auto t = std::move(*opt);

    EXPECT_EQ(node_count(t), 4u);
    EXPECT_TRUE(is_root(t, r0));
    EXPECT_TRUE(is_root(t, r1));
    EXPECT_FALSE(is_root(t, x0));
    EXPECT_FALSE(is_root(t, x1));
    EXPECT_EQ(parent(t, x0), r0);
    EXPECT_EQ(parent(t, x1), r1);

    auto c0 = children(t, r0);
    ASSERT_EQ(c0.size(), 1u);
    EXPECT_EQ(c0[0], x0);
    auto c1 = children(t, r1);
    ASSERT_EQ(c1.size(), 1u);
    EXPECT_EQ(c1[0], x1);
}

// 8. Single-parent invariant.
TEST(SharedEdgePolytree, SingleParentInvariant) {
    SharedEdgePolytreeBuilder<Belief, Msg> b;
    auto p0 = add_node(b, Belief{ 0.f });
    auto p1 = add_node(b, Belief{ 1.f });
    auto child = add_node(b, Belief{ 2.f });
    EXPECT_TRUE(add_edge(b, p0, child, Msg{ 1.f }));
    EXPECT_FALSE(add_edge(b, p1, child, Msg{ 2.f }));  // child already has a parent
    EXPECT_FALSE(add_edge(b, child, child, Msg{}));    // self-loop
    EXPECT_FALSE(add_edge(b, 99, child, Msg{}));       // out of range
}

// 9. Cycle rejection.
TEST(SharedEdgePolytree, CycleRejected) {
    SharedEdgePolytreeBuilder<Belief, Msg> b;
    auto n0 = add_node(b, Belief{ 0.f });
    auto n1 = add_node(b, Belief{ 1.f });
    auto n2 = add_node(b, Belief{ 2.f });
    // 0 -> 1 -> 2, then 2 -> 0 closes a cycle. add_edge for 2->0 is allowed by
    // the single-parent check (0 has no parent yet), so build() must catch it.
    EXPECT_TRUE(add_edge(b, n0, n1, Msg{}));
    EXPECT_TRUE(add_edge(b, n1, n2, Msg{}));
    EXPECT_TRUE(add_edge(b, n2, n0, Msg{}));
    auto opt = build(std::move(b));
    EXPECT_FALSE(opt.has_value());
}

// 10a. for_each_node visits every node exactly once, in node-id order.
TEST(SharedEdgePolytree, ForEachNodeVisitsAllInOrder) {
    auto t = make_tree();
    std::vector<NodeHandle> seen;
    for_each_node(t, [&](NodeHandle n) { seen.push_back(n); });
    EXPECT_EQ(seen, (std::vector<NodeHandle>{ R, A, B, C }));
}

// 10b. for_each_root visits only roots (parent == INVALID_NODE), in node-id
// order — including across a multi-root forest.
TEST(SharedEdgePolytree, ForEachRootVisitsRootsOnly) {
    auto t = make_tree();  // single root R
    std::vector<NodeHandle> roots;
    for_each_root(t, [&](NodeHandle n) { roots.push_back(n); });
    EXPECT_EQ(roots, (std::vector<NodeHandle>{ R }));

    // Two-root forest: 0 -> 2, with 1 a lone root.
    SharedEdgePolytreeBuilder<Belief, Msg> b;
    add_node(b, Belief{ 0.f });                  // 0 (root)
    add_node(b, Belief{ 1.f });                  // 1 (lone root)
    add_node(b, Belief{ 2.f });                  // 2 (child of 0)
    EXPECT_TRUE(add_edge(b, 0, 2, Msg{}));
    auto opt = build(std::move(b));
    ASSERT_TRUE(opt.has_value());

    std::vector<NodeHandle> froots;
    for_each_root(*opt, [&](NodeHandle n) { froots.push_back(n); });
    EXPECT_EQ(froots, (std::vector<NodeHandle>{ 0, 1 }));
}

// 10c. for_each_edge visits every parent->child edge once, in child-node-id
// order, exposing the shared edge slot (roots have no parent-edge).
TEST(SharedEdgePolytree, ForEachEdgeVisitsAllEdgesInChildOrder) {
    auto t = make_tree();  // r=0 -> {a=1, b=2}; a=1 -> c=3
    std::vector<NodeHandle> parents, kids;
    for_each_edge(t, [&](NodeHandle p, NodeHandle c, Msg&) {
        parents.push_back(p);
        kids.push_back(c);
    });
    // child-id order: c=1(p=0), c=2(p=0), c=3(p=1); root 0 emits nothing.
    EXPECT_EQ(kids, (std::vector<NodeHandle>{ A, B, C }));
    EXPECT_EQ(parents, (std::vector<NodeHandle>{ R, R, A }));

    // The visited edge is the shared slot: a write is observable via
    // edge_to_parent(child).
    for_each_edge(t, [&](NodeHandle, NodeHandle c, Msg& e) {
        e.m = static_cast<float>(c) * 2.f;
    });
    EXPECT_FLOAT_EQ(edge_to_parent(t, A).m, 2.f);
    EXPECT_FLOAT_EQ(edge_to_parent(t, B).m, 4.f);
    EXPECT_FLOAT_EQ(edge_to_parent(t, C).m, 6.f);
}

// 10. topo_order is a valid topological order; reverse is leaf->root.
TEST(SharedEdgePolytree, TopoOrderValid) {
    auto t = make_tree();
    auto topo = topo_order(t);
    ASSERT_EQ(topo.size(), node_count(t));

    // position[node] = index in topo order
    std::vector<uint32_t> pos(node_count(t), 0);
    for (uint32_t i = 0; i < topo.size(); ++i) pos[topo[i]] = i;

    // Every parent precedes its children.
    for (NodeHandle n = 0; n < node_count(t); ++n) {
        NodeHandle p = parent(t, n);
        if (p != INVALID_NODE)
            EXPECT_LT(pos[p], pos[n]) << "parent " << p << " must precede child " << n;
    }

    // Reverse order: every child precedes its parent (leaf->root).
    std::vector<NodeHandle> rev(topo.begin(), topo.end());
    std::reverse(rev.begin(), rev.end());
    std::vector<uint32_t> rpos(node_count(t), 0);
    for (uint32_t i = 0; i < rev.size(); ++i) rpos[rev[i]] = i;
    for (NodeHandle n = 0; n < node_count(t); ++n) {
        NodeHandle p = parent(t, n);
        if (p != INVALID_NODE)
            EXPECT_LT(rpos[n], rpos[p]) << "child " << n << " must precede parent " << p;
    }
}
