#pragma once

// wz/core/graph/polytree.h

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <vector>

#include <graph/static_dag.h> // NodeHandle, EdgeHandle, INVALID_NODE, detail::carve

namespace wz::core::graph {

    // ─── Polytree — pure view type, owns nothing ─────────────────────────────────
    //
    // A DAG where every node has at most one parent.
    // The in-CSR from DAG collapses to a flat parent array:
    //   parent[n] == INVALID_NODE  →  n is a root
    //   parent[n] == h             →  h is the sole parent of n
    //
    // Buffer layout (single contiguous allocation):
    //
    //   [ NodeData   * nc     ]   node_data
    //   [ uint32_t   * (nc+1) ]   out_offsets
    //   [ NodeHandle * ec     ]   out_neighbors
    //   [ EdgeData   * ec     ]   out_edge_data
    //   [ NodeHandle * nc     ]   parent
    //   [ EdgeData   * nc     ]   parent_edge_data  (parallel to parent)
    //   [ NodeHandle * nc     ]   topo_order

    template<typename NodeData, typename EdgeData>
    struct Polytree {
        std::span<const NodeData>   node_data;

        // out-CSR (children) — identical to DAG
        std::span<const uint32_t>   out_offsets;    // size = nc + 1
        std::span<const NodeHandle> out_neighbors;
        std::span<const EdgeData>   out_edge_data;  // parallel to out_neighbors

        // single parent per node
        std::span<const NodeHandle> parent;          // size = nc
        std::span<const EdgeData>   parent_edge_data;// size = nc, parallel to parent

        // cached topological order
        std::span<const NodeHandle> topo_order;
    };


    // ─── PolytreeStorage ──────────────────────────────────────────────────────────

    template<typename NodeData, typename EdgeData>
    struct PolytreeStorage {
        std::unique_ptr<std::byte[]> buffer;
        Polytree<NodeData, EdgeData> polytree;
    };


    // ─── PolytreeBuilder ──────────────────────────────────────────────────────────

    template<typename NodeData, typename EdgeData>
    struct PolytreeBuilder {
        struct PendingEdge {
            NodeHandle from, to;
            EdgeData   data;
        };

        std::vector<NodeData>    nodes;
        std::vector<PendingEdge> edges;
        std::vector<NodeHandle>  parent_of; // tracks which nodes already have a parent
    };


    // ─── Builder operations ───────────────────────────────────────────────────────

    template<typename N, typename E>
    NodeHandle add_node(PolytreeBuilder<N, E>& b, N data) {
        NodeHandle h = static_cast<NodeHandle>(b.nodes.size());
        b.nodes.push_back(std::move(data));
        b.parent_of.push_back(INVALID_NODE);
        return h;
    }

    // Returns false if:
    //   - handles are out of range
    //   - self-loop
    //   - 'to' already has a parent (polytree invariant)
    template<typename N, typename E>
    bool add_edge(PolytreeBuilder<N, E>& b, NodeHandle from, NodeHandle to, E data = {}) {
        if (from >= b.nodes.size() || to >= b.nodes.size()) return false;
        if (from == to)                                      return false;
        if (b.parent_of[to] != INVALID_NODE)                return false; // invariant
        b.parent_of[to] = from;
        b.edges.push_back({ from, to, std::move(data) });
        return true;
    }


    // ─── Polytree queries ─────────────────────────────────────────────────────────

    template<typename N, typename E>
    uint32_t node_count(const Polytree<N, E>& t) {
        return static_cast<uint32_t>(t.node_data.size());
    }

    template<typename N, typename E>
    uint32_t edge_count(const Polytree<N, E>& t) {
        return static_cast<uint32_t>(t.out_neighbors.size());
    }

    template<typename N, typename E>
    const N& node_data(const Polytree<N, E>& t, NodeHandle n) {
        return t.node_data[n];
    }

    template<typename N, typename E>
    std::span<const NodeHandle> children(const Polytree<N, E>& t, NodeHandle n) {
        return t.out_neighbors.subspan(t.out_offsets[n],
            t.out_offsets[n + 1] - t.out_offsets[n]);
    }

    // O(1) — direct array lookup, no span subrange needed
    template<typename N, typename E>
    NodeHandle parent(const Polytree<N, E>& t, NodeHandle n) {
        return t.parent[n];
    }

    template<typename N, typename E>
    const E& parent_edge_data(const Polytree<N, E>& t, NodeHandle n) {
        return t.parent_edge_data[n];
    }

    template<typename N, typename E>
    std::span<const E> outgoing_edge_data(const Polytree<N, E>& t, NodeHandle n) {
        return t.out_edge_data.subspan(t.out_offsets[n],
            t.out_offsets[n + 1] - t.out_offsets[n]);
    }

    template<typename N, typename E>
    bool is_root(const Polytree<N, E>& t, NodeHandle n) {
        return t.parent[n] == INVALID_NODE;
    }

    template<typename N, typename E>
    bool is_leaf(const Polytree<N, E>& t, NodeHandle n) {
        return t.out_offsets[n] == t.out_offsets[n + 1];
    }

    template<typename N, typename E>
    bool has_edge(const Polytree<N, E>& t, NodeHandle from, NodeHandle to) {
        auto ch = children(t, from);
        return std::find(ch.begin(), ch.end(), to) != ch.end();
    }

    template<typename N, typename E>
    std::span<const NodeHandle> topo_order(const Polytree<N, E>& t) {
        return t.topo_order;
    }

    // ─── Document-tree child helpers ──────────────────────────────────────────────

    template<typename N, typename E>
    uint32_t child_count(const Polytree<N, E>& t, NodeHandle n) {
        return t.out_offsets[n + 1] - t.out_offsets[n];
    }

    // Returns INVALID_NODE if index is out of range.
    template<typename N, typename E>
    NodeHandle child_at(const Polytree<N, E>& t, NodeHandle n, uint32_t index) {
        auto ch = children(t, n);
        if (index >= ch.size()) return INVALID_NODE;
        return ch[index];
    }

    // Returns nullptr if index is out of range.
    template<typename N, typename E>
    const E* child_edge_data_at(const Polytree<N, E>& t, NodeHandle n, uint32_t index) {
        auto ed = outgoing_edge_data(t, n);
        if (index >= ed.size()) return nullptr;
        return &ed[index];
    }


    // ─── Traversal ────────────────────────────────────────────────────────────────

    template<typename N, typename E, typename Visitor>
    void dfs(const Polytree<N, E>& t, NodeHandle root, Visitor&& visit) {
        std::vector<bool>       visited(node_count(t), false);
        std::vector<NodeHandle> stack;
        stack.push_back(root);
        while (!stack.empty()) {
            NodeHandle n = stack.back(); stack.pop_back();
            if (visited[n]) continue;
            visited[n] = true;
            visit(n);
            for (NodeHandle child : children(t, n))
                stack.push_back(child); // no visited check needed — tree structure
        }
    }

    template<typename N, typename E, typename Visitor>
    void bfs(const Polytree<N, E>& t, NodeHandle root, Visitor&& visit) {
        // No visited guard needed for children — each node has exactly one parent
        // so the child sets are disjoint. Guard kept on the queue for safety.
        std::vector<bool>       visited(node_count(t), false);
        std::vector<NodeHandle> queue;
        queue.push_back(root);
        visited[root] = true;
        for (uint32_t head = 0; head < queue.size(); ++head) {
            NodeHandle n = queue[head];
            visit(n);
            for (NodeHandle child : children(t, n))
                if (!visited[child]) { visited[child] = true; queue.push_back(child); }
        }
    }

    // Ancestor walk — O(depth), unique to polytree, not possible on DAG
    template<typename N, typename E, typename Visitor>
    void walk_ancestors(const Polytree<N, E>& t, NodeHandle n, Visitor&& visit) {
        NodeHandle cur = parent(t, n);
        while (cur != INVALID_NODE) {
            visit(cur);
            cur = parent(t, cur);
        }
    }


    // ─── build() ─────────────────────────────────────────────────────────────────

    namespace detail {

        template<typename N, typename E>
        std::vector<NodeHandle> polytree_kahn_topo(
            uint32_t nc,
            const std::vector<typename PolytreeBuilder<N, E>::PendingEdge>& edges)
        {
            std::vector<std::vector<NodeHandle>> adj(nc);
            std::vector<uint32_t>               in_degree(nc, 0);
            for (auto& e : edges) { adj[e.from].push_back(e.to); ++in_degree[e.to]; }

            std::vector<NodeHandle> queue, order;
            order.reserve(nc);
            for (uint32_t i = 0; i < nc; ++i)
                if (in_degree[i] == 0) queue.push_back(i);
            while (!queue.empty()) {
                NodeHandle n = queue.back(); queue.pop_back();
                order.push_back(n);
                for (NodeHandle c : adj[n])
                    if (--in_degree[c] == 0) queue.push_back(c);
            }
            return (order.size() == nc) ? order : std::vector<NodeHandle>{};
        }

    } // namespace detail


    // Consumes the builder. Returns nullopt if a cycle is detected.
    template<typename N, typename E>
    std::optional<PolytreeStorage<N, E>> build(PolytreeBuilder<N, E> b) {
        const uint32_t nc = static_cast<uint32_t>(b.nodes.size());
        const uint32_t ec = static_cast<uint32_t>(b.edges.size());

        auto topo = detail::polytree_kahn_topo<N, E>(nc, b.edges);
        if (topo.empty() && nc > 0) return std::nullopt;

        // Sort edges by 'from' for out-CSR
        std::sort(b.edges.begin(), b.edges.end(),
            [](auto& a, auto& x) { return a.from < x.from; });

        const size_t buf_size =
            sizeof(N) * nc + alignof(N)
            + sizeof(uint32_t) * (nc + 1) + alignof(uint32_t)
            + sizeof(NodeHandle) * ec + alignof(NodeHandle)
            + sizeof(E) * ec + alignof(E)
            + sizeof(NodeHandle) * nc + alignof(NodeHandle) // parent
            + sizeof(E) * nc + alignof(E)          // parent_edge_data
            + sizeof(NodeHandle) * nc + alignof(NodeHandle);// topo_order

        auto buf = std::make_unique<std::byte[]>(buf_size);
        std::byte* ptr = buf.get();
        std::byte* end = ptr + buf_size;

        std::span<N>          node_data_w;
        std::span<uint32_t>   out_off_w;
        std::span<NodeHandle> out_nbr_w;
        std::span<E>          out_ed_w;
        std::span<NodeHandle> parent_w;
        std::span<E>          parent_ed_w;
        std::span<NodeHandle> topo_w;

        ptr = detail::carve(ptr, end, nc, node_data_w);
        ptr = detail::carve(ptr, end, nc + 1, out_off_w);
        ptr = detail::carve(ptr, end, ec, out_nbr_w);
        ptr = detail::carve(ptr, end, ec, out_ed_w);
        ptr = detail::carve(ptr, end, nc, parent_w);
        ptr = detail::carve(ptr, end, nc, parent_ed_w);
        ptr = detail::carve(ptr, end, nc, topo_w);

        // node data
        for (uint32_t i = 0; i < nc; ++i)
            new (&node_data_w[i]) N(std::move(b.nodes[i]));

        // out-CSR
        std::fill(out_off_w.begin(), out_off_w.end(), 0u);
        for (auto& e : b.edges) ++out_off_w[e.from + 1];
        std::partial_sum(out_off_w.begin(), out_off_w.end(), out_off_w.begin());

        std::vector<uint32_t> cursor(out_off_w.begin(), out_off_w.end());
        for (auto& e : b.edges) {
            uint32_t pos = cursor[e.from]++;
            out_nbr_w[pos] = e.to;
            out_ed_w[pos] = e.data;
        }

        // parent array — sourced from builder's parent_of tracking
        for (uint32_t i = 0; i < nc; ++i)
            parent_w[i] = b.parent_of[i];

        // parent edge data — find each node's incoming edge data
        std::fill(parent_ed_w.begin(), parent_ed_w.end(), E{});
        for (auto& e : b.edges)
            parent_ed_w[e.to] = e.data;

        // topo order
        std::copy(topo.begin(), topo.end(), topo_w.begin());

        Polytree<N, E> t{
            .node_data = node_data_w,
            .out_offsets = out_off_w,
            .out_neighbors = out_nbr_w,
            .out_edge_data = out_ed_w,
            .parent = parent_w,
            .parent_edge_data = parent_ed_w,
            .topo_order = topo_w,
        };

        return PolytreeStorage<N, E>{ std::move(buf), t };
    }

} // namespace wz::core::graph