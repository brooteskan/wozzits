#include <gtest/gtest.h>
#include <graph/static_dag.h>
#include <graph/static_dag_algo.h>
#include <algo/pipeline.h>

using namespace wz::core::graph;
using namespace wz::core::algo::pipeline;

// ─── Fixtures ─────────────────────────────────────────────────────────────────

namespace {

    struct Transform { float x; bool active; };
    struct NoEdge {};

    // Collect sink for NodeHandle
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

    // Collect sink for Transform
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

    // Capacity-limited sink — fills after N pushes, triggering early termination
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

    //      root(active)
    //      /           \
    //  left(inactive)  right(active)
    //      \           /
    //      sink(active)
    //
    // root=0, left=1, right=2, sink=3

    DAGStorage<Transform, NoEdge> make_diamond()
    {
        DAGBuilder<Transform, NoEdge> b;
        add_node(b, Transform{ 1.f, true });  // 0 root
        add_node(b, Transform{ 2.f, false });  // 1 left
        add_node(b, Transform{ 3.f, true });  // 2 right
        add_node(b, Transform{ 4.f, true });  // 3 sink
        add_edge(b, 0u, 1u);
        add_edge(b, 0u, 2u);
        add_edge(b, 1u, 3u);
        add_edge(b, 2u, 3u);
        auto result = build(std::move(b));
        assert(result.has_value());
        return std::move(*result);
    }

} // namespace


// ─── Option 1: topo_order directly into a pipeline ───────────────────────────

TEST(DAGAlgoSpec, TopoOrderPipelineFiltersActiveNodes)
{
    auto storage = make_diamond();
    auto& g = storage.dag;

    auto pipe = filter([&](NodeHandle n) {
        return node_data(g, n).active;
        });

    HandleSink out;
    pipe(topo_order(g), out);

    // root, right, sink are active — left is not
    ASSERT_EQ(out.count, 3u);
    for (auto n : out.result())
        EXPECT_TRUE(node_data(g, n).active);
}

TEST(DAGAlgoSpec, TopoOrderPipelineMapsThenFilters)
{
    auto storage = make_diamond();
    auto& g = storage.dag;

    auto pipe = map([&](NodeHandle n) { return node_data(g, n); })
        | filter([](const Transform& t) { return t.active; });

    TransformSink out;
    pipe(topo_order(g), out);

    ASSERT_EQ(out.count, 3u);
    for (auto& t : out.result())
        EXPECT_TRUE(t.active);
}

TEST(DAGAlgoSpec, TopoOrderPipelineVisitsAllNodes)
{
    auto storage = make_diamond();
    auto& g = storage.dag;

    HandleSink out;
    auto pipe = filter([](NodeHandle) { return true; });
    pipe(topo_order(g), out);

    EXPECT_EQ(out.count, 4u);
}


// ─── Option 2: bfs_materialize / dfs_materialize ─────────────────────────────

TEST(DAGAlgoSpec, BFSMaterializeContainsAllNodes)
{
    auto storage = make_diamond();
    NodeHandle scratch[16];
    auto order = bfs_materialize(storage.dag, 0u, scratch);

    ASSERT_EQ(order.size(), 4u);
    for (NodeHandle h : {0u, 1u, 2u, 3u})
        EXPECT_EQ(std::count(order.begin(), order.end(), h), 1);
}

TEST(DAGAlgoSpec, BFSMaterializeRootIsFirst)
{
    auto storage = make_diamond();
    NodeHandle scratch[16];
    auto order = bfs_materialize(storage.dag, 0u, scratch);

    EXPECT_EQ(order[0], 0u);
}

TEST(DAGAlgoSpec, BFSMaterializeIntoThenPipe)
{
    auto storage = make_diamond();
    auto& g = storage.dag;

    NodeHandle scratch[16];
    auto order = bfs_materialize(g, 0u, scratch);

    auto pipe = map([&](NodeHandle n) { return node_data(g, n); })
        | filter([](const Transform& t) { return t.active; });

    TransformSink out;
    pipe(order, out);

    ASSERT_EQ(out.count, 3u);
    for (auto& t : out.result())
        EXPECT_TRUE(t.active);
}

TEST(DAGAlgoSpec, DFSMaterializeContainsAllNodes)
{
    auto storage = make_diamond();
    NodeHandle scratch[16];
    auto order = dfs_materialize(storage.dag, 0u, scratch);

    ASSERT_EQ(order.size(), 4u);
    for (NodeHandle h : {0u, 1u, 2u, 3u})
        EXPECT_EQ(std::count(order.begin(), order.end(), h), 1);
}

TEST(DAGAlgoSpec, MaterializeTruncatesWhenScratchExhausted)
{
    auto storage = make_diamond();
    NodeHandle scratch[2]; // intentionally too small
    auto order = bfs_materialize(storage.dag, 0u, scratch);

    // truncated to scratch size, not a crash
    EXPECT_EQ(order.size(), 2u);
}


// ─── Option 3: as_sink — pipeline inside traversal ───────────────────────────

TEST(DAGAlgoSpec, AsSinkDFSFiltersActiveNodes)
{
    auto storage = make_diamond();
    auto& g = storage.dag;

    auto pipe = map([&](NodeHandle n) { return node_data(g, n); })
        | filter([](const Transform& t) { return t.active; });

    TransformSink out;
    auto sink = as_sink(pipe, out);
    dfs(g, 0u, sink);

    ASSERT_EQ(out.count, 3u);
    for (auto& t : out.result())
        EXPECT_TRUE(t.active);
}

TEST(DAGAlgoSpec, AsSinkBFSFiltersActiveNodes)
{
    auto storage = make_diamond();
    auto& g = storage.dag;

    auto pipe = map([&](NodeHandle n) { return node_data(g, n); })
        | filter([](const Transform& t) { return t.active; });

    TransformSink out;
    auto sink = as_sink(pipe, out);
    bfs(g, 0u, sink);

    ASSERT_EQ(out.count, 3u);
    for (auto& t : out.result())
        EXPECT_TRUE(t.active);
}

TEST(DAGAlgoSpec, AsSinkEarlyTerminationAbortsDFS)
{
    auto storage = make_diamond();
    auto& g = storage.dag;

    // capacity 1 — DFS should stop after the first node
    LimitedSink out;
    out.capacity = 1;

    auto pipe = filter([](NodeHandle) { return true; });
    auto sink = as_sink(pipe, out);
    dfs(g, 0u, sink);

    EXPECT_EQ(out.count, 1u);
}

TEST(DAGAlgoSpec, AsSinkEarlyTerminationAbortsBFS)
{
    auto storage = make_diamond();
    auto& g = storage.dag;

    LimitedSink out;
    out.capacity = 2;

    auto pipe = filter([](NodeHandle) { return true; });
    auto sink = as_sink(pipe, out);
    bfs(g, 0u, sink);

    EXPECT_EQ(out.count, 2u);
}