#pragma once

// wz/core/graph/shared_edge_polytree.h

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <optional>
#include <span>
#include <vector>

#include <graph/static_dag.h> // NodeHandle, EdgeHandle, INVALID_NODE

namespace wz::core::graph {

    // ─── SharedEdgePolytree — owning, mutable, belief-propagation tree ────────────
    //
    // A polytree (forest; every node has at most one parent) purpose-built for
    // belief-propagation message passing. It differs from Polytree in three ways:
    //
    //   1. Single-storage, child-indexed edges. In a tree each non-root node has
    //      exactly one parent-edge, so EdgeData is stored ONCE, indexed by the
    //      CHILD node id: edge_store[c] is the edge between c and its parent
    //      (a root's slot is unused/default). Both endpoints reach the SAME slot —
    //      from the child c it is edge_store[c]; from the parent iterating its
    //      children, the edge to child c is also edge_store[c]. Unlike Polytree,
    //      which duplicates each edge into out_edge_data and parent_edge_data, the
    //      edge here is a single shared, mutable, bidirectional message slot.
    //
    //   2. Mutable payload. Topology (the parent array + children CSR) is immutable
    //      after build(), but NodeData and EdgeData are mutable through the type —
    //      no const_cast needed. The struct OWNS its vectors.
    //
    //   3. Unified neighbor iteration with exclude-target — directly serving BP's
    //      "product over neighbors except the target" rule.
    //
    // Storage:
    //   node_data[n]      — payload for node n            (mutable)
    //   edge_store[c]     — edge between c and parent(c)  (mutable, shared slot)
    //   parent[n]         — sole parent of n, or INVALID_NODE for a root
    //   child_offsets[..] — children CSR offsets (size = nc + 1)
    //   child_list[..]    — children CSR neighbor list
    //   topo_order[..]    — cached topological order (parents precede children)

    template<typename NodeData, typename EdgeData>
    struct SharedEdgePolytree {
        std::vector<NodeData>   node_data;   // size = nc, mutable payload
        std::vector<EdgeData>   edge_store;  // size = nc, child-indexed shared edges

        // immutable topology
        std::vector<NodeHandle> parent;        // size = nc; INVALID_NODE = root
        std::vector<uint32_t>   child_offsets; // size = nc + 1
        std::vector<NodeHandle> child_list;    // size = nc - root_count
        std::vector<NodeHandle> topo_order;    // size = nc
    };


    // ─── SharedEdgePolytreeBuilder ────────────────────────────────────────────────

    template<typename NodeData, typename EdgeData>
    struct SharedEdgePolytreeBuilder {
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
    NodeHandle add_node(SharedEdgePolytreeBuilder<N, E>& b, N data) {
        NodeHandle h = static_cast<NodeHandle>(b.nodes.size());
        b.nodes.push_back(std::move(data));
        b.parent_of.push_back(INVALID_NODE);
        return h;
    }

    // Returns false if:
    //   - handles are out of range
    //   - self-loop
    //   - 'to' already has a parent (single-parent invariant)
    template<typename N, typename E>
    bool add_edge(SharedEdgePolytreeBuilder<N, E>& b,
        NodeHandle from, NodeHandle to, E data = {}) {
        if (from >= b.nodes.size() || to >= b.nodes.size()) return false;
        if (from == to)                                      return false;
        if (b.parent_of[to] != INVALID_NODE)                return false; // invariant
        b.parent_of[to] = from;
        b.edges.push_back({ from, to, std::move(data) });
        return true;
    }


    // ─── Queries ──────────────────────────────────────────────────────────────────

    template<typename N, typename E>
    uint32_t node_count(const SharedEdgePolytree<N, E>& t) {
        return static_cast<uint32_t>(t.node_data.size());
    }

    template<typename N, typename E>
    N& node_data(SharedEdgePolytree<N, E>& t, NodeHandle n) {
        return t.node_data[n];
    }

    template<typename N, typename E>
    const N& node_data(const SharedEdgePolytree<N, E>& t, NodeHandle n) {
        return t.node_data[n];
    }

    template<typename N, typename E>
    std::span<const NodeHandle> children(const SharedEdgePolytree<N, E>& t, NodeHandle n) {
        return std::span<const NodeHandle>(t.child_list).subspan(
            t.child_offsets[n], t.child_offsets[n + 1] - t.child_offsets[n]);
    }

    // O(1) — direct array lookup.
    template<typename N, typename E>
    NodeHandle parent(const SharedEdgePolytree<N, E>& t, NodeHandle n) {
        return t.parent[n];
    }

    template<typename N, typename E>
    bool is_root(const SharedEdgePolytree<N, E>& t, NodeHandle n) {
        return t.parent[n] == INVALID_NODE;
    }

    template<typename N, typename E>
    bool is_leaf(const SharedEdgePolytree<N, E>& t, NodeHandle n) {
        return t.child_offsets[n] == t.child_offsets[n + 1];
    }

    template<typename N, typename E>
    uint32_t child_count(const SharedEdgePolytree<N, E>& t, NodeHandle n) {
        return t.child_offsets[n + 1] - t.child_offsets[n];
    }

    template<typename N, typename E>
    std::span<const NodeHandle> topo_order(const SharedEdgePolytree<N, E>& t) {
        return t.topo_order;
    }


    // ─── Shared edge accessors ────────────────────────────────────────────────────
    //
    // Both directions resolve to the single child-indexed slot edge_store[child].

    // The edge between non-root n and its parent. Undefined for a root.
    template<typename N, typename E>
    E& edge_to_parent(SharedEdgePolytree<N, E>& t, NodeHandle n) {
        assert(!is_root(t, n));
        return t.edge_store[n];
    }

    template<typename N, typename E>
    const E& edge_to_parent(const SharedEdgePolytree<N, E>& t, NodeHandle n) {
        assert(!is_root(t, n));
        return t.edge_store[n];
    }

    // The edge between a parent and one of its children — the SAME slot as
    // edge_to_parent(t, child). 'parent' is accepted for symmetry/asserts.
    template<typename N, typename E>
    E& edge_to_child(SharedEdgePolytree<N, E>& t, NodeHandle parent_n, NodeHandle child) {
        assert(t.parent[child] == parent_n);
        (void)parent_n;
        return t.edge_store[child];
    }

    template<typename N, typename E>
    const E& edge_to_child(const SharedEdgePolytree<N, E>& t,
        NodeHandle parent_n, NodeHandle child) {
        assert(t.parent[child] == parent_n);
        (void)parent_n;
        return t.edge_store[child];
    }


    // ─── Unified neighbor iteration ───────────────────────────────────────────────
    //
    // visitor(NodeHandle neighbor, EdgeData& edge) is invoked for the node's
    // parent (if any) and each child. 'edge' is always the shared slot:
    //   - parent neighbor → edge_store[n]      (the edge n←parent)
    //   - child  neighbor c → edge_store[c]    (the edge parent→c)

    template<typename N, typename E, typename Visitor>
    void for_each_neighbor(SharedEdgePolytree<N, E>& t, NodeHandle n, Visitor&& visit) {
        NodeHandle p = t.parent[n];
        if (p != INVALID_NODE) visit(p, t.edge_store[n]);
        for (NodeHandle c : children(t, n)) visit(c, t.edge_store[c]);
    }

    // Same as for_each_neighbor, but skips the neighbor whose handle == exclude.
    // Directly serves BP's "product over neighbors except the target" rule.
    template<typename N, typename E, typename Visitor>
    void for_each_neighbor_except(SharedEdgePolytree<N, E>& t, NodeHandle n,
        NodeHandle exclude, Visitor&& visit) {
        NodeHandle p = t.parent[n];
        if (p != INVALID_NODE && p != exclude) visit(p, t.edge_store[n]);
        for (NodeHandle c : children(t, n))
            if (c != exclude) visit(c, t.edge_store[c]);
    }


    // ─── Traversal ────────────────────────────────────────────────────────────────

    // Flat enumeration of every node, in node-id order — the scaffold that
    // replaces a raw `for (n = 0; n < node_count; ++n)` at call sites that touch
    // each node independently (e.g. reading a per-node marginal). It carries no
    // parent/child dependency, so a parallel driver (#293) can fan it out as a
    // flat parallel-for behind this same signature. visitor(NodeHandle n).
    template<typename N, typename E, typename Visitor>
    void for_each_node(const SharedEdgePolytree<N, E>& t, Visitor&& visit) {
        const uint32_t nc = node_count(t);
        for (NodeHandle n = 0; n < nc; ++n) visit(n);
    }

    // Enumerate the forest's roots (parent == INVALID_NODE), in node-id order.
    // Used to drive the per-subtree BP sweeps over a multi-root forest.
    // visitor(NodeHandle root).
    template<typename N, typename E, typename Visitor>
    void for_each_root(const SharedEdgePolytree<N, E>& t, Visitor&& visit) {
        const uint32_t nc = node_count(t);
        for (NodeHandle n = 0; n < nc; ++n)
            if (is_root(t, n)) visit(n);
    }

    template<typename N, typename E, typename Visitor>
    void dfs(const SharedEdgePolytree<N, E>& t, NodeHandle root, Visitor&& visit) {
        std::vector<bool>       visited(node_count(t), false);
        std::vector<NodeHandle> stack;
        stack.push_back(root);
        while (!stack.empty()) {
            NodeHandle n = stack.back(); stack.pop_back();
            if (visited[n]) continue;
            visited[n] = true;
            visit(n);
            for (NodeHandle child : children(t, n))
                stack.push_back(child); // tree structure — no extra visited check
        }
    }

    template<typename N, typename E, typename Visitor>
    void bfs(const SharedEdgePolytree<N, E>& t, NodeHandle root, Visitor&& visit) {
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


    // ─── build() ─────────────────────────────────────────────────────────────────

    namespace detail {

        template<typename N, typename E>
        std::vector<NodeHandle> shared_edge_kahn_topo(
            uint32_t nc,
            const std::vector<typename SharedEdgePolytreeBuilder<N, E>::PendingEdge>& edges)
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
    // Supports a FOREST (multiple roots).
    template<typename N, typename E>
    std::optional<SharedEdgePolytree<N, E>> build(SharedEdgePolytreeBuilder<N, E> b) {
        const uint32_t nc = static_cast<uint32_t>(b.nodes.size());
        const uint32_t ec = static_cast<uint32_t>(b.edges.size());

        auto topo = detail::shared_edge_kahn_topo<N, E>(nc, b.edges);
        if (topo.empty() && nc > 0) return std::nullopt;

        SharedEdgePolytree<N, E> t;

        // node payload (moved out of the builder)
        t.node_data = std::move(b.nodes);

        // parent array — sourced from the builder's single-parent tracking
        t.parent = std::move(b.parent_of);

        // child-indexed edge store: edge_store[child] = that child's parent-edge.
        // A root's slot stays default-constructed.
        t.edge_store.assign(nc, E{});
        for (auto& e : b.edges)
            t.edge_store[e.to] = std::move(e.data);

        // children CSR — counts bucketed by 'from'
        t.child_offsets.assign(nc + 1, 0u);
        for (auto& e : b.edges) ++t.child_offsets[e.from + 1];
        std::partial_sum(t.child_offsets.begin(), t.child_offsets.end(),
            t.child_offsets.begin());

        t.child_list.assign(ec, INVALID_NODE);
        std::vector<uint32_t> cursor(t.child_offsets.begin(), t.child_offsets.end());
        for (auto& e : b.edges)
            t.child_list[cursor[e.from]++] = e.to;

        t.topo_order = std::move(topo);

        return t;
    }

} // namespace wz::core::graph
