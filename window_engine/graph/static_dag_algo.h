// wz/core/graph/static_dag_algo.h
#pragma once

#include <graph/static_dag.h>
#include <graph/concepts.h>
#include <algo/pipeline.h>
#include <concepts>
#include <span>
#include <vector>

namespace wz::core::graph {


    // ─── Sink-based traversal ─────────────────────────────────────────────────────
    //
    // Distinct from the visitor overloads — push() returning false
    // aborts the traversal immediately, before the visited set is fully expanded.

    template<typename N, typename E, typename S>
        requires Sink<S, NodeHandle>
    void dfs(const DAG<N, E>& g, NodeHandle root, S& sink)
    {
        std::vector<bool>       visited(node_count(g), false);
        std::vector<NodeHandle> stack;
        stack.push_back(root);

        while (!stack.empty()) {
            NodeHandle n = stack.back(); stack.pop_back();
            if (visited[n]) continue;
            visited[n] = true;
            if (!sink.push(n)) return; // early termination
            for (NodeHandle child : children(g, n))
                if (!visited[child]) stack.push_back(child);
        }
    }

    template<typename N, typename E, typename S>
        requires Sink<S, NodeHandle>
    void bfs(const DAG<N, E>& g, NodeHandle root, S& sink)
    {
        std::vector<bool>       visited(node_count(g), false);
        std::vector<NodeHandle> queue;
        queue.push_back(root);
        visited[root] = true;

        for (uint32_t head = 0; head < queue.size(); ++head) {
            NodeHandle n = queue[head];
            if (!sink.push(n)) return; // early termination
            for (NodeHandle child : children(g, n))
                if (!visited[child]) { visited[child] = true; queue.push_back(child); }
        }
    }


    // ─── Materialization ──────────────────────────────────────────────────────────
    //
    // Caller provides the scratch buffer — no allocation inside.
    // Returns a subspan of scratch containing the traversal order.
    // If scratch is exhausted the traversal is truncated (consistent
    // with algo::next / algo::pipeline truncation policy).

    namespace detail {

        struct SpanSink {
            std::span<NodeHandle> buf;
            uint32_t              count = 0;

            bool push(NodeHandle n) {
                if (count >= static_cast<uint32_t>(buf.size())) return false;
                buf[count++] = n;
                return true;
            }

            std::span<NodeHandle> result() const {
                return buf.subspan(0, count);
            }
        };

    } // namespace detail

    template<typename N, typename E>
    std::span<NodeHandle> dfs_materialize(
        const DAG<N, E>& g,
        NodeHandle            root,
        std::span<NodeHandle> scratch)
    {
        detail::SpanSink sink{ scratch };
        dfs(g, root, sink);
        return sink.result();
    }

    template<typename N, typename E>
    std::span<NodeHandle> bfs_materialize(
        const DAG<N, E>& g,
        NodeHandle            root,
        std::span<NodeHandle> scratch)
    {
        detail::SpanSink sink{ scratch };
        bfs(g, root, sink);
        return sink.result();
    }


    // ─── Topo order is already a span — no materialization needed ─────────────────
    //
    // topo_order(g) returns std::span<const NodeHandle> directly.
    // Pass it straight into a pipeline.


    // ─── Pipeline adapter ─────────────────────────────────────────────────────────
    //
    // Connects a pipeline_t to a graph traversal as the driving range.
    // Usage:
    //
    //   NodeHandle scratch[64];
    //   auto order = bfs_materialize(g, root, scratch);
    //
    //   auto pipe = algo::pipeline::map([&](NodeHandle n) { return node_data(g, n); })
    //             | algo::pipeline::filter([](const Transform& t) { return t.x > 0.f; });
    //
    //   pipe(order, out);
    //
    // Or, using topo order directly (no scratch needed):
    //
    //   pipe(topo_order(g), out);
    //
    // Or, for early-terminating search — wrap a pipeline as a sink:

    template<typename Pipeline, typename Out>
    struct PipelineSink {
        const Pipeline& pipe;
        Out& out;

        bool push(NodeHandle n) {
            return pipe.apply_all(n, out);
        }
    };

    template<typename Pipeline, typename Out>
    PipelineSink<Pipeline, Out> as_sink(const Pipeline& pipe, Out& out) {
        return { pipe, out };
    }

    // Usage with early termination:
    //
    //   auto pipe = algo::pipeline::map([&](NodeHandle n) { return node_data(g, n); })
    //             | algo::pipeline::filter([](const Transform& t) { return t.active; });
    //
    //   auto sink = as_sink(pipe, out);
    //   dfs(g, root, sink);  // stops as soon as out is full

} // namespace wz::core::graph