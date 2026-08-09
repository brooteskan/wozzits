#pragma once

// wz/core/graph/shared_edge_graph.h

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

#include <graph/static_dag.h> // NodeHandle, INVALID_NODE

namespace wz::core::graph {

    // ─── SharedEdgeGraph — owning, mutable, general (cyclic) undirected graph ──────
    //
    // The loopy analog of SharedEdgePolytree: a GENERAL undirected graph where
    // cycles are allowed. Like the polytree it is purpose-built for belief
    // propagation, and it keeps the polytree's defining property — a single,
    // shared, mutable edge slot reachable from BOTH endpoints — but drops the
    // acyclicity and single-parent restrictions.
    //
    //   1. Single-storage, shared edges. Each undirected edge is stored ONCE in
    //      edge_store[e]. In the CSR adjacency the edge appears in BOTH endpoints'
    //      neighbor lists, but both incidences carry the SAME edge_store index —
    //      so a mutation made through one endpoint is visible through the other.
    //      The edge is a single shared, mutable, bidirectional message slot.
    //
    //   2. Mutable payload. Topology (the adjacency CSR) is immutable after
    //      build(), but NodeData and EdgeData are mutable through the type — no
    //      const_cast needed. The struct OWNS its vectors.
    //
    //   3. Unified neighbor iteration handing back the shared slot — directly
    //      serving loopy BP's per-edge message updates.
    //
    // Storage:
    //   node_data[n]      — payload for node n                         (mutable)
    //   edge_store[e]     — the single slot for undirected edge e      (mutable, shared)
    //   adj_offsets[..]   — adjacency CSR offsets (size = nc + 1)
    //   adj_neighbor[..]  — adjacency CSR neighbor list (size = 2 * ec)
    //   adj_edge[..]      — parallel edge_store indices  (size = 2 * ec)

    template<typename NodeData, typename EdgeData>
    struct SharedEdgeGraph {
        std::vector<NodeData> node_data;    // size = nc, mutable payload
        std::vector<EdgeData> edge_store;   // size = ec, one shared slot per edge

        // immutable adjacency (CSR). Each undirected edge appears in BOTH
        // endpoints' lists, but both incidences point at the SAME edge_store index.
        std::vector<uint32_t>   adj_offsets;   // size = nc + 1
        std::vector<NodeHandle> adj_neighbor;  // size = 2 * ec
        std::vector<uint32_t>   adj_edge;      // size = 2 * ec (index into edge_store)
    };


    // ─── SharedEdgeGraphBuilder ───────────────────────────────────────────────────

    template<typename NodeData, typename EdgeData>
    struct SharedEdgeGraphBuilder {
        struct PendingEdge {
            NodeHandle a, b;
            EdgeData   data;
        };

        std::vector<NodeData>    nodes;
        std::vector<PendingEdge> edges;
    };


    // ─── Builder operations ───────────────────────────────────────────────────────

    template<typename N, typename E>
    NodeHandle add_node(SharedEdgeGraphBuilder<N, E>& b, N data) {
        NodeHandle h = static_cast<NodeHandle>(b.nodes.size());
        b.nodes.push_back(std::move(data));
        return h;
    }

    // Returns false if:
    //   - handles are out of range
    //   - self-loop (a == b)
    // Parallel edges (the same pair added twice) are ALLOWED — each is stored as
    // its own edge; callers that want simple graphs canonicalize upstream.
    template<typename N, typename E>
    bool add_edge(SharedEdgeGraphBuilder<N, E>& b,
        NodeHandle a, NodeHandle b_node, E data = {}) {
        if (a >= b.nodes.size() || b_node >= b.nodes.size()) return false;
        if (a == b_node)                                      return false; // self-loop
        b.edges.push_back({ a, b_node, std::move(data) });
        return true;
    }


    // ─── Queries ──────────────────────────────────────────────────────────────────

    template<typename N, typename E>
    uint32_t node_count(const SharedEdgeGraph<N, E>& t) {
        return static_cast<uint32_t>(t.node_data.size());
    }

    template<typename N, typename E>
    uint32_t edge_count(const SharedEdgeGraph<N, E>& t) {
        return static_cast<uint32_t>(t.edge_store.size());
    }

    template<typename N, typename E>
    N& node_data(SharedEdgeGraph<N, E>& t, NodeHandle n) {
        return t.node_data[n];
    }

    template<typename N, typename E>
    const N& node_data(const SharedEdgeGraph<N, E>& t, NodeHandle n) {
        return t.node_data[n];
    }

    template<typename N, typename E>
    uint32_t degree(const SharedEdgeGraph<N, E>& t, NodeHandle n) {
        return t.adj_offsets[n + 1] - t.adj_offsets[n];
    }


    // ─── Unified neighbor iteration ───────────────────────────────────────────────
    //
    // visitor(NodeHandle neighbor, EdgeData& edge) is invoked once per incident
    // edge. 'edge' is always the shared slot edge_store[e], so a write made from
    // one endpoint is observed from the other. Non-const because BP mutates edges.

    template<typename N, typename E, typename Visitor>
    void for_each_neighbor(SharedEdgeGraph<N, E>& t, NodeHandle n, Visitor&& visit) {
        for (uint32_t i = t.adj_offsets[n]; i < t.adj_offsets[n + 1]; ++i)
            visit(t.adj_neighbor[i], t.edge_store[t.adj_edge[i]]);
    }


    // ─── Flat node / edge enumeration ─────────────────────────────────────────────
    //
    // Scaffolds that replace raw `for (n = 0; n < node_count; ++n)` and `for (e =
    // 0; e < edge_count; ++e)` sweeps at call sites, so those seams can later be
    // driven by a parallel driver behind the same signature. for_each_neighbor
    // (above) already covers the "each undirected edge once, from its lower-handle
    // endpoint" idiom; these cover the flat node-space and edge-index sweeps.

    // Visit every node once, in node-id order. visitor(NodeHandle n).
    template<typename N, typename E, typename Visitor>
    void for_each_node(const SharedEdgeGraph<N, E>& t, Visitor&& visit) {
        const uint32_t nc = node_count(t);
        for (NodeHandle n = 0; n < nc; ++n) visit(n);
    }

    // Visit every node once, in REVERSE node-id order — the mirror sweep a
    // forward-then-backward gauge pass needs. visitor(NodeHandle n).
    template<typename N, typename E, typename Visitor>
    void for_each_node_reverse(const SharedEdgeGraph<N, E>& t, Visitor&& visit) {
        for (NodeHandle n = node_count(t); n-- > 0;) visit(n);
    }

    // Visit every undirected edge once, by edge-store index — the id that indexes
    // edge_store and any parallel per-edge arrays (e.g. BP message buffers).
    // visitor(uint32_t edge_id). Use for_each_neighbor when you need an edge's
    // endpoints or its shared payload slot.
    template<typename N, typename E, typename Visitor>
    void for_each_edge(const SharedEdgeGraph<N, E>& t, Visitor&& visit) {
        const uint32_t ec = edge_count(t);
        for (uint32_t e = 0; e < ec; ++e) visit(e);
    }


    // ─── build() ─────────────────────────────────────────────────────────────────
    //
    // Consumes the builder. ALWAYS succeeds — cycles are fine, there is no failure
    // mode once add_edge has validated each edge — so it returns by value.
    //
    // Standard two-pass counting-CSR: count each node's degree (every edge
    // increments BOTH endpoints), prefix-sum into adj_offsets, then fill
    // adj_neighbor/adj_edge with a per-node cursor. For edge e = (a, b), a's list
    // gets (neighbor = b, edge = e) and b's list gets (neighbor = a, edge = e).

    template<typename N, typename E>
    SharedEdgeGraph<N, E> build(SharedEdgeGraphBuilder<N, E> b) {
        const uint32_t nc = static_cast<uint32_t>(b.nodes.size());
        const uint32_t ec = static_cast<uint32_t>(b.edges.size());

        SharedEdgeGraph<N, E> t;

        // node payload (moved out of the builder)
        t.node_data = std::move(b.nodes);

        // one shared slot per undirected edge, indexed by edge id
        t.edge_store.assign(ec, E{});
        for (uint32_t e = 0; e < ec; ++e)
            t.edge_store[e] = std::move(b.edges[e].data);

        // adjacency CSR — count degrees (both endpoints of every edge)
        t.adj_offsets.assign(nc + 1, 0u);
        for (auto& e : b.edges) {
            ++t.adj_offsets[e.a + 1];
            ++t.adj_offsets[e.b + 1];
        }
        std::partial_sum(t.adj_offsets.begin(), t.adj_offsets.end(),
            t.adj_offsets.begin());

        // fill both incidences of each edge with a per-node cursor
        t.adj_neighbor.assign(2 * ec, INVALID_NODE);
        t.adj_edge.assign(2 * ec, 0u);
        std::vector<uint32_t> cursor(t.adj_offsets.begin(), t.adj_offsets.end());
        for (uint32_t e = 0; e < ec; ++e) {
            const NodeHandle a = b.edges[e].a;
            const NodeHandle bb = b.edges[e].b;
            uint32_t pa = cursor[a]++;
            t.adj_neighbor[pa] = bb;
            t.adj_edge[pa]     = e;
            uint32_t pb = cursor[bb]++;
            t.adj_neighbor[pb] = a;
            t.adj_edge[pb]     = e;
        }

        return t;
    }

} // namespace wz::core::graph
