#pragma once

// wz/core/graph/dag.h

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <vector>

namespace wz::core::graph {

    // ─── Handles ─────────────────────────────────────────────────────────────────

    using NodeHandle = uint32_t;
    using EdgeHandle = uint32_t;

    static constexpr NodeHandle INVALID_NODE = 0xFFFF'FFFFu;
    static constexpr EdgeHandle INVALID_EDGE = 0xFFFF'FFFEu;


    // ─── DAG — pure view type, owns nothing ──────────────────────────────────────
    //
    // All spans point into a single contiguous backing allocation managed
    // externally (see DAGStorage below, or your arena of choice).
    //
    // For node i, outgoing edges occupy:
    //   out_neighbors [ out_offsets[i] .. out_offsets[i+1] )
    // and parallel:
    //   out_edge_data [ out_offsets[i] .. out_offsets[i+1] )

    template<typename NodeData, typename EdgeData>
    struct DAG {
        std::span<const NodeData>   node_data;

        // out-CSR
        std::span<const uint32_t>   out_offsets;    // size = node_count + 1
        std::span<const NodeHandle> out_neighbors;
        std::span<const EdgeData>   out_edge_data;  // parallel to out_neighbors

        // in-CSR
        std::span<const uint32_t>   in_offsets;     // size = node_count + 1
        std::span<const NodeHandle> in_neighbors;
        std::span<const EdgeData>   in_edge_data;   // parallel to in_neighbors

        // cached topological order
        std::span<const NodeHandle> topo_order;
    };


    // ─── DAGStorage — owns the backing allocation ─────────────────────────────────
    //
    // Holds a single heap block and the DAG view into it.
    // Replace this with your arena: allocate the block from the arena,
    // drop DAGStorage entirely, and let the arena manage lifetime.

    template<typename NodeData, typename EdgeData>
    struct DAGStorage {
        std::unique_ptr<std::byte[]> buffer;
        DAG<NodeData, EdgeData>      dag;
    };


    // ─── DAGBuilder ──────────────────────────────────────────────────────────────

    template<typename NodeData, typename EdgeData>
    struct DAGBuilder {
        struct PendingEdge {
            NodeHandle from, to;
            EdgeData   data;
        };

        std::vector<NodeData>    nodes;
        std::vector<PendingEdge> edges;
    };


    // ─── Builder operations ───────────────────────────────────────────────────────

    template<typename N, typename E>
    NodeHandle add_node(DAGBuilder<N, E>& b, N data) {
        NodeHandle h = static_cast<NodeHandle>(b.nodes.size());
        b.nodes.push_back(std::move(data));
        return h;
    }

    template<typename N, typename E>
    bool add_edge(DAGBuilder<N, E>& b, NodeHandle from, NodeHandle to, E data = {}) {
        if (from >= b.nodes.size() || to >= b.nodes.size()) return false;
        if (from == to) return false;
        b.edges.push_back({ from, to, std::move(data) });
        return true;
    }


    // ─── DAG queries ──────────────────────────────────────────────────────────────

    template<typename N, typename E>
    uint32_t node_count(const DAG<N, E>& g) {
        return static_cast<uint32_t>(g.node_data.size());
    }

    template<typename N, typename E>
    uint32_t edge_count(const DAG<N, E>& g) {
        return static_cast<uint32_t>(g.out_neighbors.size());
    }

    template<typename N, typename E>
    const N& node_data(const DAG<N, E>& g, NodeHandle n) {
        return g.node_data[n];
    }

    template<typename N, typename E>
    std::span<const NodeHandle> children(const DAG<N, E>& g, NodeHandle n) {
        return g.out_neighbors.subspan(g.out_offsets[n],
            g.out_offsets[n + 1] - g.out_offsets[n]);
    }

    template<typename N, typename E>
    std::span<const NodeHandle> parents(const DAG<N, E>& g, NodeHandle n) {
        return g.in_neighbors.subspan(g.in_offsets[n],
            g.in_offsets[n + 1] - g.in_offsets[n]);
    }

    template<typename N, typename E>
    std::span<const E> outgoing_edge_data(const DAG<N, E>& g, NodeHandle n) {
        return g.out_edge_data.subspan(g.out_offsets[n],
            g.out_offsets[n + 1] - g.out_offsets[n]);
    }

    template<typename N, typename E>
    bool has_edge(const DAG<N, E>& g, NodeHandle from, NodeHandle to) {
        auto ch = children(g, from);
        return std::find(ch.begin(), ch.end(), to) != ch.end();
    }

    template<typename N, typename E>
    bool is_root(const DAG<N, E>& g, NodeHandle n) {
        return g.in_offsets[n] == g.in_offsets[n + 1];
    }

    template<typename N, typename E>
    bool is_leaf(const DAG<N, E>& g, NodeHandle n) {
        return g.out_offsets[n] == g.out_offsets[n + 1];
    }

    template<typename N, typename E>
    std::span<const NodeHandle> topo_order(const DAG<N, E>& g) {
        return g.topo_order;
    }


    // ─── Traversal ────────────────────────────────────────────────────────────────

    template<typename N, typename E, typename Visitor>
    void dfs(const DAG<N, E>& g, NodeHandle root, Visitor&& visit) {
        std::vector<bool>       visited(node_count(g), false);
        std::vector<NodeHandle> stack;
        stack.push_back(root);
        while (!stack.empty()) {
            NodeHandle n = stack.back(); stack.pop_back();
            if (visited[n]) continue;
            visited[n] = true;
            visit(n);
            for (NodeHandle child : children(g, n))
                if (!visited[child]) stack.push_back(child);
        }
    }

    template<typename N, typename E, typename Visitor>
    void bfs(const DAG<N, E>& g, NodeHandle root, Visitor&& visit) {
        std::vector<bool>       visited(node_count(g), false);
        std::vector<NodeHandle> queue;
        queue.push_back(root);
        visited[root] = true;
        for (uint32_t head = 0; head < queue.size(); ++head) {
            NodeHandle n = queue[head];
            visit(n);
            for (NodeHandle child : children(g, n))
                if (!visited[child]) { visited[child] = true; queue.push_back(child); }
        }
    }


    // ─── build() — internal helpers ───────────────────────────────────────────────

    namespace detail {

        // Alignment-aware pointer advance within a flat buffer.
        // Advances ptr to the next address satisfying alignof(T),
        // writes a span of count T objects into out, and returns the end pointer.
        template<typename T>
        std::byte* carve(std::byte* ptr, std::byte* end, size_t count,
            std::span<T>& out)
        {
            constexpr size_t align = alignof(T);
            auto addr = reinterpret_cast<uintptr_t>(ptr);
            addr = (addr + align - 1) & ~(align - 1);
            ptr = reinterpret_cast<std::byte*>(addr);

            assert(ptr + count * sizeof(T) <= end);
            out = { reinterpret_cast<T*>(ptr), count };
            return ptr + count * sizeof(T);
        }

        template<typename N, typename E>
        std::vector<NodeHandle> kahn_topo(
            uint32_t nc,
            const std::vector<typename DAGBuilder<N, E>::PendingEdge>& edges)
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

        // Fill a pre-carved CSR triple (offsets, neighbors, edge_data)
        // sorted by 'key' (from = out-CSR, to = in-CSR).
        template<typename N, typename E>
        void fill_csr(
            uint32_t nc,
            const std::vector<typename DAGBuilder<N, E>::PendingEdge>& edges,
            bool by_from,
            std::span<uint32_t>   offsets,
            std::span<NodeHandle> neighbors,
            std::span<E>          edata)
        {
            std::vector<size_t> order(edges.size());
            std::iota(order.begin(), order.end(), 0);
            std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                return by_from ? edges[a].from < edges[b].from
                    : edges[a].to < edges[b].to;
                });

            std::fill(offsets.begin(), offsets.end(), 0u);
            for (auto& e : edges)
                ++offsets[by_from ? e.from + 1 : e.to + 1];
            std::partial_sum(offsets.begin(), offsets.end(), offsets.begin());

            std::vector<uint32_t> cursor(offsets.begin(), offsets.end());
            for (size_t idx : order) {
                auto& e = edges[idx];
                uint32_t pos = cursor[by_from ? e.from : e.to]++;
                neighbors[pos] = by_from ? e.to : e.from;
                edata[pos] = e.data;
            }
        }

    } // namespace detail


    // ─── build() ─────────────────────────────────────────────────────────────────
    //
    // Lays out all seven arrays in a single allocation.
    // Layout (each section aligned to its type):
    //
    //   [ NodeData * nc ]
    //   [ uint32_t * (nc+1) ]   out_offsets
    //   [ NodeHandle * ec  ]    out_neighbors
    //   [ EdgeData * ec    ]    out_edge_data
    //   [ uint32_t * (nc+1) ]   in_offsets
    //   [ NodeHandle * ec  ]    in_neighbors
    //   [ EdgeData * ec    ]    in_edge_data
    //   [ NodeHandle * nc  ]    topo_order
    //
    // Returns nullopt if a cycle is detected.

    template<typename N, typename E>
    std::optional<DAGStorage<N, E>> build(DAGBuilder<N, E> b) {
        const uint32_t nc = static_cast<uint32_t>(b.nodes.size());
        const uint32_t ec = static_cast<uint32_t>(b.edges.size());

        auto topo = detail::kahn_topo<N, E>(nc, b.edges);
        if (topo.empty() && nc > 0) return std::nullopt;

        // Calculate buffer size with alignment padding (generous upper bound).
        const size_t buf_size =
            sizeof(N) * nc + alignof(N)
            + sizeof(uint32_t) * (nc + 1) + alignof(uint32_t)
            + sizeof(NodeHandle) * ec + alignof(NodeHandle)
            + sizeof(E) * ec + alignof(E)
            + sizeof(uint32_t) * (nc + 1) + alignof(uint32_t)
            + sizeof(NodeHandle) * ec + alignof(NodeHandle)
            + sizeof(E) * ec + alignof(E)
            + sizeof(NodeHandle) * nc + alignof(NodeHandle);

        auto buf = std::make_unique<std::byte[]>(buf_size);
        std::byte* ptr = buf.get();
        std::byte* end = ptr + buf_size;

        // Mutable spans for writing during construction.
        std::span<N>          node_data_w;
        std::span<uint32_t>   out_off_w;
        std::span<NodeHandle> out_nbr_w;
        std::span<E>          out_ed_w;
        std::span<uint32_t>   in_off_w;
        std::span<NodeHandle> in_nbr_w;
        std::span<E>          in_ed_w;
        std::span<NodeHandle> topo_w;

        ptr = detail::carve(ptr, end, nc, node_data_w);
        ptr = detail::carve(ptr, end, nc + 1, out_off_w);
        ptr = detail::carve(ptr, end, ec, out_nbr_w);
        ptr = detail::carve(ptr, end, ec, out_ed_w);
        ptr = detail::carve(ptr, end, nc + 1, in_off_w);
        ptr = detail::carve(ptr, end, ec, in_nbr_w);
        ptr = detail::carve(ptr, end, ec, in_ed_w);
        ptr = detail::carve(ptr, end, nc, topo_w);

        // Write node data
        for (uint32_t i = 0; i < nc; ++i)
            new (&node_data_w[i]) N(std::move(b.nodes[i]));

        // Write topo order
        std::copy(topo.begin(), topo.end(), topo_w.begin());

        // Write CSR tables
        detail::fill_csr<N, E>(nc, b.edges, true, out_off_w, out_nbr_w, out_ed_w);
        detail::fill_csr<N, E>(nc, b.edges, false, in_off_w, in_nbr_w, in_ed_w);

        // Build the const view
        DAG<N, E> g{
            .node_data = node_data_w,
            .out_offsets = out_off_w,
            .out_neighbors = out_nbr_w,
            .out_edge_data = out_ed_w,
            .in_offsets = in_off_w,
            .in_neighbors = in_nbr_w,
            .in_edge_data = in_ed_w,
            .topo_order = topo_w,
        };

        return DAGStorage<N, E>{ std::move(buf), g };
    }

} // namespace wz::core::graph