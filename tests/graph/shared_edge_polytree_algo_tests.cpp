#include <graph/shared_edge_polytree_algo.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

using namespace wz::core::graph;

namespace
{
    // A bond carries one message per direction: up = child->parent,
    // down = parent->child. The BP scaffold only sequences the sweeps; this
    // test supplies the message algebra.
    struct Msg
    {
        double up = 0.0;
        double down = 0.0;
    };

    using Tree = SharedEdgePolytree<double, Msg>;

    std::optional<Tree> make_tree(
        const std::vector<double>& values,
        const std::vector<std::pair<int, int>>& edges)
    {
        SharedEdgePolytreeBuilder<double, Msg> b;
        for (double v : values) {
            add_node(b, v);
        }
        for (auto [from, to] : edges) {
            if (!add_edge(b, static_cast<NodeHandle>(from),
                    static_cast<NodeHandle>(to), Msg{})) {
                return std::nullopt;
            }
        }
        return build(std::move(b));
    }

    // Run exact two-sweep BP with a caller-supplied combine op. The message a
    // node sends along an edge is its own value combined with the incoming
    // messages from all its OTHER neighbors (exclude-the-target).
    template<typename Combine>
    void run_bp(Tree& t, Combine combine)
    {
        belief_propagation(
            t,
            // collect (leaf->root): message toward the parent.
            [&](NodeHandle n, NodeHandle p, Msg& edge) {
                double acc = node_data(t, n);
                for_each_neighbor_except(t, n, p,
                    [&](NodeHandle, Msg& en) { acc = combine(acc, en.up); });
                edge.up = acc;
            },
            // distribute (root->leaf): message toward each child.
            [&](NodeHandle n, NodeHandle c, Msg& edge) {
                double acc = node_data(t, n);
                for_each_neighbor_except(t, n, c, [&](NodeHandle nb, Msg& en) {
                    acc = combine(acc, (nb == parent(t, n)) ? en.down : en.up);
                });
                edge.down = acc;
            });
    }

    // A node's belief = its value combined with every incoming message.
    template<typename Combine>
    double belief(Tree& t, NodeHandle n, Combine combine)
    {
        double acc = node_data(t, n);
        for_each_neighbor(t, n, [&](NodeHandle nb, Msg& edge) {
            acc = combine(acc, (nb == parent(t, n)) ? edge.down : edge.up);
        });
        return acc;
    }

    auto sum = [](double a, double b) { return a + b; };
    auto maxop = [](double a, double b) { return std::max(a, b); };
}

// On a tree, sum-product BP converges in two sweeps so EVERY node's belief
// equals the total over the whole tree -- the canonical exactness check.
TEST(SharedEdgePolytreeBP, SumAggregatesWholeTreeAtEveryNode_Chain)
{
    auto t = make_tree({ 1, 2, 3, 4 }, { { 0, 1 }, { 1, 2 }, { 2, 3 } });
    ASSERT_TRUE(t.has_value());
    run_bp(*t, sum);
    for (uint32_t n = 0; n < node_count(*t); ++n) {
        EXPECT_DOUBLE_EQ(belief(*t, n, sum), 10.0);
    }
}

TEST(SharedEdgePolytreeBP, SumAggregatesWholeTreeAtEveryNode_Star)
{
    auto t = make_tree({ 1, 2, 3, 4 }, { { 0, 1 }, { 0, 2 }, { 0, 3 } });
    ASSERT_TRUE(t.has_value());
    run_bp(*t, sum);
    for (uint32_t n = 0; n < node_count(*t); ++n) {
        EXPECT_DOUBLE_EQ(belief(*t, n, sum), 10.0);
    }
}

TEST(SharedEdgePolytreeBP, SumAggregatesWholeTreeAtEveryNode_Balanced)
{
    // 0 -> {1, 2}; 1 -> {3, 4}
    auto t = make_tree(
        { 1, 2, 3, 4, 5 },
        { { 0, 1 }, { 0, 2 }, { 1, 3 }, { 1, 4 } });
    ASSERT_TRUE(t.has_value());
    run_bp(*t, sum);
    for (uint32_t n = 0; n < node_count(*t); ++n) {
        EXPECT_DOUBLE_EQ(belief(*t, n, sum), 15.0);
    }
}

TEST(SharedEdgePolytreeBP, SingleNodeBeliefIsItsOwnValue)
{
    auto t = make_tree({ 7 }, {});
    ASSERT_TRUE(t.has_value());
    run_bp(*t, sum);
    EXPECT_DOUBLE_EQ(belief(*t, 0, sum), 7.0);
}

// The scaffold is generic over the algebra: max-product on a tree puts the
// GLOBAL max at every node's belief.
TEST(SharedEdgePolytreeBP, MaxProductGivesGlobalMaxAtEveryNode)
{
    auto t = make_tree(
        { 3, 9, 2, 5, 4 },
        { { 0, 1 }, { 0, 2 }, { 1, 3 }, { 1, 4 } });
    ASSERT_TRUE(t.has_value());
    run_bp(*t, maxop);
    for (uint32_t n = 0; n < node_count(*t); ++n) {
        EXPECT_DOUBLE_EQ(belief(*t, n, maxop), 9.0);
    }
}

// A forest of two disjoint trees: BP over the shared structure aggregates each
// node only over ITS OWN tree (no leakage across components).
TEST(SharedEdgePolytreeBP, ForestAggregatesPerTree)
{
    // Tree A: 0 -> {1, 2}  (sum 6).  Tree B: 3 -> 4  (sum 30).
    auto t = make_tree(
        { 1, 2, 3, 10, 20 },
        { { 0, 1 }, { 0, 2 }, { 3, 4 } });
    ASSERT_TRUE(t.has_value());
    run_bp(*t, sum);
    EXPECT_DOUBLE_EQ(belief(*t, 0, sum), 6.0);
    EXPECT_DOUBLE_EQ(belief(*t, 1, sum), 6.0);
    EXPECT_DOUBLE_EQ(belief(*t, 2, sum), 6.0);
    EXPECT_DOUBLE_EQ(belief(*t, 3, sum), 30.0);
    EXPECT_DOUBLE_EQ(belief(*t, 4, sum), 30.0);
}


// ─── Traversal-order guards ───────────────────────────────────────────────────
//
// The BP drivers are dependency-explicit recursions (post-order collect, pre-
// order distribute) precisely so #293 can swap a parallel run/wait driver in
// behind them. That swap stays bit-exact against the serial oracle ONLY while two
// properties hold, so pin them here:
//   1. each parent's children are visited in fixed CSR (child_list) order — the
//      order the message kernels fold them in;
//   2. the sweeps respect the child/parent dependency (collect: children before
//      parent; distribute: parent before child).

// (1) distribute hands each parent its children in CSR order — even when children
// were added out of node-id order, so an accidental id-sort would be caught.
TEST(SharedEdgePolytreeBP, DistributeVisitsChildrenInCsrOrder)
{
    // 0 -> children added in edge order {2, 1, 3};  1 -> {5, 4}.
    auto t = make_tree(
        { 0, 0, 0, 0, 0, 0 },
        { { 0, 2 }, { 0, 1 }, { 0, 3 }, { 1, 5 }, { 1, 4 } });
    ASSERT_TRUE(t.has_value());

    std::vector<std::vector<NodeHandle>> seen(node_count(*t));
    bp_distribute(*t, [&](NodeHandle n, NodeHandle c, Msg&) {
        seen[n].push_back(c);
    });

    for (NodeHandle n = 0; n < node_count(*t); ++n) {
        auto ch = children(*t, n);
        const std::vector<NodeHandle> expected(ch.begin(), ch.end());
        EXPECT_EQ(seen[n], expected)
            << "parent " << n << " must emit to children in CSR order";
    }
    // Guard the fixture itself: node 0's children are genuinely non-ascending.
    EXPECT_EQ(seen[0], (std::vector<NodeHandle>{ 2, 1, 3 }));
    EXPECT_EQ(seen[1], (std::vector<NodeHandle>{ 5, 4 }));
}

// (2a) collect is post-order: every non-root node is visited only after all its
// children. (A root sends no up-message, so it is never emitted.)
TEST(SharedEdgePolytreeBP, CollectVisitsChildrenBeforeParent)
{
    // 0 -> {1, 2}; 1 -> {3, 4}.
    auto t = make_tree(
        { 0, 0, 0, 0, 0 },
        { { 0, 1 }, { 0, 2 }, { 1, 3 }, { 1, 4 } });
    ASSERT_TRUE(t.has_value());

    std::vector<int> step(node_count(*t), -1);
    int clock = 0;
    bp_collect(*t, [&](NodeHandle n, NodeHandle, Msg&) { step[n] = clock++; });

    EXPECT_EQ(step[0], -1) << "root emits no up-message";
    for (NodeHandle n = 0; n < node_count(*t); ++n) {
        for (NodeHandle c : children(*t, n)) {
            EXPECT_GE(step[c], 0) << "non-root child " << c << " must be collected";
            if (step[n] >= 0)  // n itself non-root -> it was emitted too
                EXPECT_LT(step[c], step[n])
                    << "child " << c << " must be collected before parent " << n;
        }
    }
}

// (2b) distribute is pre-order: a node receives its own down-message before it
// hands one to any of its children.
TEST(SharedEdgePolytreeBP, DistributeVisitsParentBeforeChild)
{
    // 0 -> {1, 2}; 1 -> {3, 4}.
    auto t = make_tree(
        { 0, 0, 0, 0, 0 },
        { { 0, 1 }, { 0, 2 }, { 1, 3 }, { 1, 4 } });
    ASSERT_TRUE(t.has_value());

    // distribute(parent, c) is the moment c's incoming down-message is written.
    std::vector<int> got_down(node_count(*t), -1);
    int clock = 0;
    bp_distribute(*t, [&](NodeHandle, NodeHandle c, Msg&) { got_down[c] = clock++; });

    EXPECT_EQ(got_down[0], -1) << "root receives no down-message";
    for (NodeHandle n = 0; n < node_count(*t); ++n) {
        for (NodeHandle c : children(*t, n)) {
            EXPECT_GE(got_down[c], 0) << "non-root child " << c << " must receive a down";
            if (got_down[n] >= 0)  // n itself non-root -> it received a down first
                EXPECT_LT(got_down[n], got_down[c])
                    << "parent " << n << " must receive its down before child " << c;
        }
    }
}
