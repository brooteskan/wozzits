#pragma once

// wz/core/graph/polytree_algo.h

#include <graph/static_polytree.h>
#include <graph/concepts.h>
#include <algo/pipeline.h>
#include <span>
#include <vector>

namespace wz::core::graph {



    // ─── Sink-based traversal ─────────────────────────────────────────────────────

    template<typename N, typename E, typename S>
        requires Sink<S, NodeHandle>
    void dfs(const Polytree<N, E>& t, NodeHandle root, S& sink)
    {
        std::vector<bool>       visited(node_count(t), false);
        std::vector<NodeHandle> stack;
        stack.push_back(root);
        while (!stack.empty()) {
            NodeHandle n = stack.back(); stack.pop_back();
            if (visited[n]) continue;
            visited[n] = true;
            if (!sink.push(n)) return;
            for (NodeHandle child : children(t, n))
                stack.push_back(child);
        }
    }

    template<typename N, typename E, typename S>
        requires Sink<S, NodeHandle>
    void bfs(const Polytree<N, E>& t, NodeHandle root, S& sink)
    {
        std::vector<bool>       visited(node_count(t), false);
        std::vector<NodeHandle> queue;
        queue.push_back(root);
        visited[root] = true;
        for (uint32_t head = 0; head < queue.size(); ++head) {
            NodeHandle n = queue[head];
            if (!sink.push(n)) return;
            for (NodeHandle child : children(t, n))
                if (!visited[child]) { visited[child] = true; queue.push_back(child); }
        }
    }

    // Ancestor walk with sink — unique to polytree.
    // Walks from n toward the root, stopping if push() returns false.
    template<typename N, typename E, typename S>
        requires Sink<S, NodeHandle>
    void walk_ancestors(const Polytree<N, E>& t, NodeHandle n, S& sink)
    {
        NodeHandle cur = parent(t, n);
        while (cur != INVALID_NODE) {
            if (!sink.push(cur)) return;
            cur = parent(t, cur);
        }
    }


    // ─── Materialization ──────────────────────────────────────────────────────────

    namespace detail {

        struct PolytreeSpanSink {
            std::span<NodeHandle> buf;
            uint32_t              count = 0;

            bool push(NodeHandle n) {
                if (count >= static_cast<uint32_t>(buf.size())) return false;
                buf[count++] = n;
                return true;
            }

            std::span<NodeHandle> result() const { return buf.subspan(0, count); }
        };

    } // namespace detail

    template<typename N, typename E>
    std::span<NodeHandle> dfs_materialize(
        const Polytree<N, E>& t,
        NodeHandle            root,
        std::span<NodeHandle> scratch)
    {
        detail::PolytreeSpanSink sink{ scratch };
        dfs(t, root, sink);
        return sink.result();
    }

    template<typename N, typename E>
    std::span<NodeHandle> bfs_materialize(
        const Polytree<N, E>& t,
        NodeHandle            root,
        std::span<NodeHandle> scratch)
    {
        detail::PolytreeSpanSink sink{ scratch };
        bfs(t, root, sink);
        return sink.result();
    }

    // Ancestor walk materialization — produces a span of ancestors
    // ordered from immediate parent to root, truncated if scratch is exhausted.
    template<typename N, typename E>
    std::span<NodeHandle> ancestors_materialize(
        const Polytree<N, E>& t,
        NodeHandle            n,
        std::span<NodeHandle> scratch)
    {
        detail::PolytreeSpanSink sink{ scratch };
        walk_ancestors(t, n, sink);
        return sink.result();
    }


    // ─── Document-tree helpers ────────────────────────────────────────────────────

    // Returns UINT32_MAX for roots (no parent).
    template<typename N, typename E>
    uint32_t child_ordinal(const Polytree<N, E>& t, NodeHandle n) {
        NodeHandle par = parent(t, n);
        if (par == INVALID_NODE) return UINT32_MAX;
        auto ch = children(t, par);
        for (uint32_t i = 0; i < ch.size(); ++i)
            if (ch[i] == n) return i;
        return UINT32_MAX;
    }

    // Returns INVALID_NODE for first children and roots.
    template<typename N, typename E>
    NodeHandle previous_sibling(const Polytree<N, E>& t, NodeHandle n) {
        uint32_t ord = child_ordinal(t, n);
        if (ord == UINT32_MAX || ord == 0) return INVALID_NODE;
        return child_at(t, parent(t, n), ord - 1);
    }

    // Returns INVALID_NODE for last children and roots.
    template<typename N, typename E>
    NodeHandle next_sibling(const Polytree<N, E>& t, NodeHandle n) {
        uint32_t ord = child_ordinal(t, n);
        if (ord == UINT32_MAX) return INVALID_NODE;
        return child_at(t, parent(t, n), ord + 1);
    }

    template<typename N, typename E>
    uint32_t depth(const Polytree<N, E>& t, NodeHandle n) {
        uint32_t   d   = 0;
        NodeHandle cur = parent(t, n);
        while (cur != INVALID_NODE) { ++d; cur = parent(t, cur); }
        return d;
    }

    // Materializes all roots into scratch. Truncates silently if scratch is exhausted.
    template<typename N, typename E>
    std::span<NodeHandle> roots_materialize(
        const Polytree<N, E>& t,
        std::span<NodeHandle> scratch)
    {
        uint32_t count = 0;
        for (uint32_t i = 0; i < node_count(t) && count < scratch.size(); ++i)
            if (is_root(t, i)) scratch[count++] = i;
        return scratch.subspan(0, count);
    }

    // Materializes ancestors from root down to the immediate parent of n.
    // Excludes n itself. Truncates silently if scratch is exhausted.
    template<typename N, typename E>
    std::span<NodeHandle> ancestors_materialize_root_first(
        const Polytree<N, E>& t,
        NodeHandle            n,
        std::span<NodeHandle> scratch)
    {
        detail::PolytreeSpanSink sink{ scratch };
        walk_ancestors(t, n, sink);
        auto filled = sink.result();
        std::reverse(filled.begin(), filled.end());
        return filled;
    }

    template<typename N, typename E>
    uint32_t subtree_size(const Polytree<N, E>& t, NodeHandle root) {
        uint32_t count = 0;
        dfs(t, root, [&](NodeHandle) { ++count; });
        return count;
    }

    // Returns INVALID_NODE if no child matches.
    // Predicate: (NodeHandle child, const E& edge_data, uint32_t ordinal) -> bool
    template<typename N, typename E, typename Predicate>
    NodeHandle find_child_if(const Polytree<N, E>& t, NodeHandle n, Predicate&& pred) {
        auto ch = children(t, n);
        auto ed = outgoing_edge_data(t, n);
        for (uint32_t i = 0; i < ch.size(); ++i)
            if (pred(ch[i], ed[i], i)) return ch[i];
        return INVALID_NODE;
    }

    // Walks the root-to-node path, calling visitor(parent, child, edge_data, ordinal)
    // for each edge. Returns false if node is INVALID_NODE or scratch cannot hold the
    // full path (requires depth(t, node) + 1 slots). Returns true and calls visitor
    // zero times when node is a root.
    template<typename N, typename E, typename Visitor>
    bool walk_path_from_root(
        const Polytree<N, E>& t,
        NodeHandle            node,
        std::span<NodeHandle> scratch,
        Visitor&&             visitor)
    {
        if (node == INVALID_NODE) return false;

        // Walk ancestors parent-first into scratch[0..d-1].
        uint32_t   d   = 0;
        NodeHandle cur = parent(t, node);
        while (cur != INVALID_NODE) {
            if (d >= scratch.size()) return false;
            scratch[d++] = cur;
            cur = parent(t, cur);
        }
        // Need one more slot for node itself.
        if (d >= scratch.size()) return false;

        std::reverse(scratch.begin(), scratch.begin() + d);
        scratch[d] = node;
        auto path = scratch.subspan(0, d + 1);

        // Scan each parent's child span once to get ordinal and edge data together.
        for (uint32_t i = 0; i + 1 < path.size(); ++i) {
            NodeHandle par   = path[i];
            NodeHandle child = path[i + 1];
            auto ch = children(t, par);
            auto ed = outgoing_edge_data(t, par);
            for (uint32_t j = 0; j < ch.size(); ++j) {
                if (ch[j] == child) { visitor(par, child, ed[j], j); break; }
            }
        }
        return true;
    }


    // ─── Pipeline adapter ─────────────────────────────────────────────────────────

    template<typename Pipeline, typename Out>
    struct PolytreePipelineSink {
        const Pipeline& pipe;
        Out& out;

        bool push(NodeHandle n) {
            return pipe.apply_all(n, out);
        }
    };

    template<typename Pipeline, typename Out>
    PolytreePipelineSink<Pipeline, Out> as_sink(const Pipeline& pipe, Out& out) {
        return { pipe, out };
    }

} // namespace wz::core::graph