#include <gtest/gtest.h>
#include <graph/static_polytree.h>
#include <graph/static_polytree_algo.h>
#include <algo/pipeline.h>

using namespace wz::core::graph;
using namespace wz::core::algo::pipeline;

// ─── Fixtures ─────────────────────────────────────────────────────────────────
//
// Chain:        root(active) --> mid(inactive) --> leaf(active)
//
// Wide:              root(active)
//                   /     |     \
//           a(active) b(inactive) c(active)
//                         |
//                      d(active)

namespace {

    struct Transform { float x; bool active; };
    struct NoEdge {};

    struct HandleSink {
        NodeHandle buf[16];
        uint32_t   count = 0;
        bool push(NodeHandle n) {
            if (count >= 16) return false;
            buf[count++] = n;
            return true;
        }
        std::span<const NodeHandle> result() const { return { buf, count }; }
    };

    struct TransformSink {
        Transform buf[16];
        uint32_t  count = 0;
        bool push(Transform t) {
            if (count >= 16) return false;
            buf[count++] = t;
            return true;
        }
        std::span<const Transform> result() const { return { buf, count }; }
    };

    struct LimitedSink {
        NodeHandle buf[16];
        uint32_t   count = 0;
        uint32_t   capacity = 16;
        bool push(NodeHandle n) {
            if (count >= capacity) return false;
            buf[count++] = n;
            return true;
        }
    };

    PolytreeStorage<Transform, NoEdge> make_chain()
    {
        PolytreeBuilder<Transform, NoEdge> b;
        add_node(b, Transform{ 0.f, true });  // 0 root
        add_node(b, Transform{ 1.f, false });  // 1 mid
        add_node(b, Transform{ 2.f, true });  // 2 leaf
        add_edge(b, 0u, 1u);
        add_edge(b, 1u, 2u);
        auto result = build(std::move(b));
        assert(result.has_value());
        return std::move(*result);
    }

    PolytreeStorage<Transform, NoEdge> make_wide()
    {
        PolytreeBuilder<Transform, NoEdge> b;
        add_node(b, Transform{ 0.f, true });  // 0 root
        add_node(b, Transform{ 1.f, true });  // 1 a
        add_node(b, Transform{ 2.f, false });  // 2 b
        add_node(b, Transform{ 3.f, true });  // 3 c
        add_node(b, Transform{ 4.f, true });  // 4 d
        add_edge(b, 0u, 1u);
        add_edge(b, 0u, 2u);
        add_edge(b, 0u, 3u);
        add_edge(b, 2u, 4u);
        auto result = build(std::move(b));
        assert(result.has_value());
        return std::move(*result);
    }

    struct DocEdge { std::string_view key; };

    //
    // DocTree:          root(0)
    //                  /   |   \
    //               a(1)  b(4)  c(5)
    //              /   \          \
    //           a0(2)  a1(3)      c0(6)
    //
    PolytreeStorage<NoEdge, DocEdge> make_doc_tree()
    {
        PolytreeBuilder<NoEdge, DocEdge> b;
        add_node(b, NoEdge{});  // 0 root
        add_node(b, NoEdge{});  // 1 a
        add_node(b, NoEdge{});  // 2 a0
        add_node(b, NoEdge{});  // 3 a1
        add_node(b, NoEdge{});  // 4 b
        add_node(b, NoEdge{});  // 5 c
        add_node(b, NoEdge{});  // 6 c0
        add_edge(b, 0u, 1u, DocEdge{"a"});
        add_edge(b, 0u, 4u, DocEdge{"b"});
        add_edge(b, 0u, 5u, DocEdge{"c"});
        add_edge(b, 1u, 2u, DocEdge{"a0"});
        add_edge(b, 1u, 3u, DocEdge{"a1"});
        add_edge(b, 5u, 6u, DocEdge{"c0"});
        auto result = build(std::move(b));
        assert(result.has_value());
        return std::move(*result);
    }

    //
    // Forest:   root0(0)    root1(2)
    //              |            |
    //            a(1)          b(3)
    //
    PolytreeStorage<NoEdge, NoEdge> make_forest()
    {
        PolytreeBuilder<NoEdge, NoEdge> b;
        add_node(b, NoEdge{});  // 0 root0
        add_node(b, NoEdge{});  // 1 a
        add_node(b, NoEdge{});  // 2 root1
        add_node(b, NoEdge{});  // 3 b
        add_edge(b, 0u, 1u);
        add_edge(b, 2u, 3u);
        auto result = build(std::move(b));
        assert(result.has_value());
        return std::move(*result);
    }

} // namespace


// ─── Option 1: topo_order directly into pipeline ─────────────────────────────

TEST(PolytreeAlgoSpec, TopoOrderPipelineFiltersActiveNodes)
{
    auto storage = make_wide();
    auto& t = storage.polytree;

    auto pipe = filter([&](NodeHandle n) { return node_data(t, n).active; });

    HandleSink out;
    pipe(topo_order(t), out);

    // root, a, c, d are active — b is not
    ASSERT_EQ(out.count, 4u);
    for (auto n : out.result())
        EXPECT_TRUE(node_data(t, n).active);
}

TEST(PolytreeAlgoSpec, TopoOrderPipelineMapsThenFilters)
{
    auto storage = make_chain();
    auto& t = storage.polytree;

    auto pipe = map([&](NodeHandle n) { return node_data(t, n); })
        | filter([](const Transform& x) { return x.active; });

    TransformSink out;
    pipe(topo_order(t), out);

    // root and leaf are active, mid is not
    ASSERT_EQ(out.count, 2u);
    for (auto& x : out.result())
        EXPECT_TRUE(x.active);
}


// ─── Option 2: materialization ────────────────────────────────────────────────

TEST(PolytreeAlgoSpec, BFSMaterializeContainsAllNodes)
{
    auto storage = make_wide();
    NodeHandle scratch[16];
    auto order = bfs_materialize(storage.polytree, 0u, scratch);

    ASSERT_EQ(order.size(), 5u);
    for (NodeHandle h : {0u, 1u, 2u, 3u, 4u})
        EXPECT_EQ(std::count(order.begin(), order.end(), h), 1);
}

TEST(PolytreeAlgoSpec, DFSMaterializeContainsAllNodes)
{
    auto storage = make_wide();
    NodeHandle scratch[16];
    auto order = dfs_materialize(storage.polytree, 0u, scratch);

    ASSERT_EQ(order.size(), 5u);
    for (NodeHandle h : {0u, 1u, 2u, 3u, 4u})
        EXPECT_EQ(std::count(order.begin(), order.end(), h), 1);
}

TEST(PolytreeAlgoSpec, MaterializeTruncatesWhenScratchExhausted)
{
    auto storage = make_wide();
    NodeHandle scratch[2];
    auto order = bfs_materialize(storage.polytree, 0u, scratch);

    EXPECT_EQ(order.size(), 2u);
}

TEST(PolytreeAlgoSpec, BFSMaterializeIntoThenPipe)
{
    auto storage = make_wide();
    auto& t = storage.polytree;

    NodeHandle scratch[16];
    auto order = bfs_materialize(t, 0u, scratch);

    auto pipe = map([&](NodeHandle n) { return node_data(t, n); })
        | filter([](const Transform& x) { return x.active; });

    TransformSink out;
    pipe(order, out);

    ASSERT_EQ(out.count, 4u);
    for (auto& x : out.result())
        EXPECT_TRUE(x.active);
}


// ─── Option 3: as_sink ────────────────────────────────────────────────────────

TEST(PolytreeAlgoSpec, AsSinkBFSFiltersActiveNodes)
{
    auto storage = make_wide();
    auto& t = storage.polytree;

    auto pipe = map([&](NodeHandle n) { return node_data(t, n); })
        | filter([](const Transform& x) { return x.active; });

    TransformSink out;
    auto sink = as_sink(pipe, out);
    bfs(t, 0u, sink);

    ASSERT_EQ(out.count, 4u);
    for (auto& x : out.result())
        EXPECT_TRUE(x.active);
}

TEST(PolytreeAlgoSpec, AsSinkEarlyTerminationAbortsBFS)
{
    auto storage = make_wide();

    LimitedSink out;
    out.capacity = 2;

    auto pipe = filter([](NodeHandle) { return true; });
    auto sink = as_sink(pipe, out);
    bfs(storage.polytree, 0u, sink);

    EXPECT_EQ(out.count, 2u);
}

TEST(PolytreeAlgoSpec, AsSinkDFSFiltersActiveNodes)
{
    auto storage = make_chain();
    auto& t = storage.polytree;

    auto pipe = map([&](NodeHandle n) { return node_data(t, n); })
        | filter([](const Transform& x) { return x.active; });

    TransformSink out;
    auto sink = as_sink(pipe, out);
    dfs(t, 0u, sink);

    ASSERT_EQ(out.count, 2u);
    for (auto& x : out.result())
        EXPECT_TRUE(x.active);
}


// ─── Ancestor walk — unique to polytree ───────────────────────────────────────

TEST(PolytreeAlgoSpec, AncestorsMaterializeFromLeaf)
{
    auto storage = make_chain();
    NodeHandle scratch[16];
    auto anc = ancestors_materialize(storage.polytree, 2u, scratch);

    ASSERT_EQ(anc.size(), 2u);
    EXPECT_EQ(anc[0], 1u); // immediate parent first
    EXPECT_EQ(anc[1], 0u); // then root
}

TEST(PolytreeAlgoSpec, AncestorsMaterializeFromRootIsEmpty)
{
    auto storage = make_chain();
    NodeHandle scratch[16];
    auto anc = ancestors_materialize(storage.polytree, 0u, scratch);

    EXPECT_EQ(anc.size(), 0u);
}

TEST(PolytreeAlgoSpec, AncestorsMaterializeTruncates)
{
    auto storage = make_chain();
    NodeHandle scratch[1]; // leaf has 2 ancestors — truncates to 1
    auto anc = ancestors_materialize(storage.polytree, 2u, scratch);

    EXPECT_EQ(anc.size(), 1u);
    EXPECT_EQ(anc[0], 1u); // immediate parent only
}

TEST(PolytreeAlgoSpec, AncestorWalkSinkEarlyTermination)
{
    auto storage = make_chain();

    LimitedSink out;
    out.capacity = 1;
    walk_ancestors(storage.polytree, 2u, out);

    EXPECT_EQ(out.count, 1u);
    EXPECT_EQ(out.buf[0], 1u);
}

TEST(PolytreeAlgoSpec, AncestorWalkPipelineFilters)
{
    auto storage = make_wide();
    auto& t = storage.polytree;

    // d(4) has ancestors: b(2, inactive), root(0, active)
    auto pipe = filter([&](NodeHandle n) { return node_data(t, n).active; });

    HandleSink out;
    auto sink = as_sink(pipe, out);
    walk_ancestors(t, 4u, sink);

    ASSERT_EQ(out.count, 1u);
    EXPECT_EQ(out.buf[0], 0u); // only root passes the filter
}


// ─── Document-tree child helpers ─────────────────────────────────────────────

TEST(PolytreeAlgoSpec, ChildCount)
{
    auto storage = make_doc_tree();
    auto& t = storage.polytree;

    EXPECT_EQ(child_count(t, 0u), 3u); // root: a, b, c
    EXPECT_EQ(child_count(t, 1u), 2u); // a: a0, a1
    EXPECT_EQ(child_count(t, 5u), 1u); // c: c0
    EXPECT_EQ(child_count(t, 4u), 0u); // b: leaf
}

TEST(PolytreeAlgoSpec, ChildAtInRange)
{
    auto storage = make_doc_tree();
    auto& t = storage.polytree;

    EXPECT_EQ(child_at(t, 0u, 0u), 1u); // root[0] == a
    EXPECT_EQ(child_at(t, 0u, 1u), 4u); // root[1] == b
    EXPECT_EQ(child_at(t, 0u, 2u), 5u); // root[2] == c
    EXPECT_EQ(child_at(t, 1u, 0u), 2u); // a[0] == a0
    EXPECT_EQ(child_at(t, 1u, 1u), 3u); // a[1] == a1
}

TEST(PolytreeAlgoSpec, ChildAtOutOfRange)
{
    auto storage = make_doc_tree();
    auto& t = storage.polytree;

    EXPECT_EQ(child_at(t, 0u, 3u), INVALID_NODE); // root has 3 children
    EXPECT_EQ(child_at(t, 4u, 0u), INVALID_NODE); // b is a leaf
}

TEST(PolytreeAlgoSpec, ChildEdgeDataAtInRange)
{
    auto storage = make_doc_tree();
    auto& t = storage.polytree;

    const DocEdge* ed0 = child_edge_data_at(t, 0u, 0u);
    const DocEdge* ed1 = child_edge_data_at(t, 0u, 1u);
    const DocEdge* ed2 = child_edge_data_at(t, 0u, 2u);

    ASSERT_NE(ed0, nullptr); EXPECT_EQ(ed0->key, "a");
    ASSERT_NE(ed1, nullptr); EXPECT_EQ(ed1->key, "b");
    ASSERT_NE(ed2, nullptr); EXPECT_EQ(ed2->key, "c");
}

TEST(PolytreeAlgoSpec, ChildEdgeDataAtOutOfRange)
{
    auto storage = make_doc_tree();
    auto& t = storage.polytree;

    EXPECT_EQ(child_edge_data_at(t, 0u, 3u), nullptr); // root has 3 children
    EXPECT_EQ(child_edge_data_at(t, 4u, 0u), nullptr); // b is a leaf
}


// ─── Document-tree ordinal and siblings ──────────────────────────────────────

TEST(PolytreeAlgoSpec, ChildOrdinalRoot)
{
    auto storage = make_doc_tree();
    EXPECT_EQ(child_ordinal(storage.polytree, 0u), UINT32_MAX);
}

TEST(PolytreeAlgoSpec, ChildOrdinalNonRoot)
{
    auto storage = make_doc_tree();
    auto& t = storage.polytree;

    EXPECT_EQ(child_ordinal(t, 1u), 0u); // a is root's first child
    EXPECT_EQ(child_ordinal(t, 4u), 1u); // b is root's second child
    EXPECT_EQ(child_ordinal(t, 5u), 2u); // c is root's third child
    EXPECT_EQ(child_ordinal(t, 2u), 0u); // a0 is a's first child
    EXPECT_EQ(child_ordinal(t, 3u), 1u); // a1 is a's second child
}

TEST(PolytreeAlgoSpec, PreviousSibling)
{
    auto storage = make_doc_tree();
    auto& t = storage.polytree;

    EXPECT_EQ(previous_sibling(t, 4u), 1u); // b's prev == a
    EXPECT_EQ(previous_sibling(t, 5u), 4u); // c's prev == b
    EXPECT_EQ(previous_sibling(t, 1u), INVALID_NODE); // a is first child
    EXPECT_EQ(previous_sibling(t, 0u), INVALID_NODE); // root has no parent
}

TEST(PolytreeAlgoSpec, NextSibling)
{
    auto storage = make_doc_tree();
    auto& t = storage.polytree;

    EXPECT_EQ(next_sibling(t, 1u), 4u); // a's next == b
    EXPECT_EQ(next_sibling(t, 4u), 5u); // b's next == c
    EXPECT_EQ(next_sibling(t, 5u), INVALID_NODE); // c is last child
    EXPECT_EQ(next_sibling(t, 0u), INVALID_NODE); // root has no parent
}


// ─── Document-tree depth and roots ───────────────────────────────────────────

TEST(PolytreeAlgoSpec, Depth)
{
    auto storage = make_doc_tree();
    auto& t = storage.polytree;

    EXPECT_EQ(depth(t, 0u), 0u); // root
    EXPECT_EQ(depth(t, 1u), 1u); // a
    EXPECT_EQ(depth(t, 4u), 1u); // b
    EXPECT_EQ(depth(t, 2u), 2u); // a0
    EXPECT_EQ(depth(t, 3u), 2u); // a1
    EXPECT_EQ(depth(t, 6u), 2u); // c0
}

TEST(PolytreeAlgoSpec, RootsMaterializeSingleRoot)
{
    auto storage = make_doc_tree();
    NodeHandle scratch[16];
    auto roots = roots_materialize(storage.polytree, scratch);

    ASSERT_EQ(roots.size(), 1u);
    EXPECT_EQ(roots[0], 0u);
}

TEST(PolytreeAlgoSpec, RootsMaterializeForest)
{
    auto storage = make_forest();
    NodeHandle scratch[16];
    auto roots = roots_materialize(storage.polytree, scratch);

    ASSERT_EQ(roots.size(), 2u);
    EXPECT_EQ(roots[0], 0u); // root0
    EXPECT_EQ(roots[1], 2u); // root1
}


// ─── Document-tree ancestor materialization ───────────────────────────────────

TEST(PolytreeAlgoSpec, AncestorsMaterializeRootFirst)
{
    auto storage = make_doc_tree();
    NodeHandle scratch[16];
    auto anc = ancestors_materialize_root_first(storage.polytree, 3u, scratch); // a1

    ASSERT_EQ(anc.size(), 2u);
    EXPECT_EQ(anc[0], 0u); // root first
    EXPECT_EQ(anc[1], 1u); // then a
}

TEST(PolytreeAlgoSpec, AncestorsMaterializeRootFirstFromRoot)
{
    auto storage = make_doc_tree();
    NodeHandle scratch[16];
    auto anc = ancestors_materialize_root_first(storage.polytree, 0u, scratch);

    EXPECT_EQ(anc.size(), 0u);
}

TEST(PolytreeAlgoSpec, AncestorsMaterializeRootFirstTruncates)
{
    auto storage = make_doc_tree();
    NodeHandle scratch[1]; // a1 has 2 ancestors — truncates to 1
    auto anc = ancestors_materialize_root_first(storage.polytree, 3u, scratch);

    EXPECT_EQ(anc.size(), 1u);
    // truncated: only the closer ancestor (a) fits before reversal clips root
    EXPECT_EQ(anc[0], 1u);
}


// ─── Document-tree subtree and search ────────────────────────────────────────

TEST(PolytreeAlgoSpec, SubtreeSize)
{
    auto storage = make_doc_tree();
    auto& t = storage.polytree;

    EXPECT_EQ(subtree_size(t, 0u), 7u); // whole tree
    EXPECT_EQ(subtree_size(t, 1u), 3u); // a, a0, a1
    EXPECT_EQ(subtree_size(t, 5u), 2u); // c, c0
    EXPECT_EQ(subtree_size(t, 4u), 1u); // b (leaf)
}

TEST(PolytreeAlgoSpec, FindChildIf)
{
    auto storage = make_doc_tree();
    auto& t = storage.polytree;

    NodeHandle found = find_child_if(t, 0u,
        [](NodeHandle, const DocEdge& ed, uint32_t) { return ed.key == "b"; });
    EXPECT_EQ(found, 4u);

    NodeHandle not_found = find_child_if(t, 0u,
        [](NodeHandle, const DocEdge& ed, uint32_t) { return ed.key == "x"; });
    EXPECT_EQ(not_found, INVALID_NODE);
}

TEST(PolytreeAlgoSpec, FindChildIfUsesOrdinal)
{
    auto storage = make_doc_tree();
    auto& t = storage.polytree;

    // find second child of root (ordinal == 1)
    NodeHandle found = find_child_if(t, 0u,
        [](NodeHandle, const DocEdge&, uint32_t ord) { return ord == 1u; });
    EXPECT_EQ(found, 4u); // b is at ordinal 1
}


// ─── Document-tree walk_path_from_root ───────────────────────────────────────

TEST(PolytreeAlgoSpec, WalkPathFromRoot)
{
    auto storage = make_doc_tree();
    auto& t = storage.polytree;

    struct PathEdge { NodeHandle par, child; std::string_view key; uint32_t ord; };
    std::vector<PathEdge> visited;

    NodeHandle scratch[16];
    bool ok = walk_path_from_root(t, 3u, scratch,  // a1
        [&](NodeHandle p, NodeHandle c, const DocEdge& ed, uint32_t ord) {
            visited.push_back({ p, c, ed.key, ord });
        });

    ASSERT_TRUE(ok);
    ASSERT_EQ(visited.size(), 2u);
    EXPECT_EQ(visited[0].par, 0u); EXPECT_EQ(visited[0].child, 1u); // root -> a
    EXPECT_EQ(visited[0].key, "a"); EXPECT_EQ(visited[0].ord, 0u);
    EXPECT_EQ(visited[1].par, 1u); EXPECT_EQ(visited[1].child, 3u); // a -> a1
    EXPECT_EQ(visited[1].key, "a1"); EXPECT_EQ(visited[1].ord, 1u);
}

TEST(PolytreeAlgoSpec, WalkPathFromRootForRootNode)
{
    auto storage = make_doc_tree();

    NodeHandle scratch[16];
    int visits = 0;
    bool ok = walk_path_from_root(storage.polytree, 0u, scratch,
        [&](NodeHandle, NodeHandle, const DocEdge&, uint32_t) { ++visits; });

    EXPECT_TRUE(ok);
    EXPECT_EQ(visits, 0);
}

TEST(PolytreeAlgoSpec, WalkPathFromRootInvalidNode)
{
    auto storage = make_doc_tree();
    NodeHandle scratch[16];
    bool ok = walk_path_from_root(storage.polytree, INVALID_NODE, scratch,
        [](NodeHandle, NodeHandle, const DocEdge&, uint32_t) {});
    EXPECT_FALSE(ok);
}

TEST(PolytreeAlgoSpec, WalkPathFromRootScratchTooSmall)
{
    auto storage = make_doc_tree();
    auto& t = storage.polytree;

    // a1 (node 3) is at depth 2 — needs 3 slots
    {
        EXPECT_FALSE(walk_path_from_root(t, 3u, std::span<NodeHandle>{},
            [](NodeHandle, NodeHandle, const DocEdge&, uint32_t) {}));
    }
    {
        NodeHandle scratch[2];
        EXPECT_FALSE(walk_path_from_root(t, 3u, scratch,
            [](NodeHandle, NodeHandle, const DocEdge&, uint32_t) {}));
    }
    {
        NodeHandle scratch[3];
        EXPECT_TRUE(walk_path_from_root(t, 3u, scratch,
            [](NodeHandle, NodeHandle, const DocEdge&, uint32_t) {}));
    }
    // root needs exactly 1 slot
    {
        EXPECT_FALSE(walk_path_from_root(t, 0u, std::span<NodeHandle>{},
            [](NodeHandle, NodeHandle, const DocEdge&, uint32_t) {}));
    }
    {
        NodeHandle scratch[1];
        EXPECT_TRUE(walk_path_from_root(t, 0u, scratch,
            [](NodeHandle, NodeHandle, const DocEdge&, uint32_t) {}));
    }
}