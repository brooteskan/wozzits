#pragma once

// wz/core/graph/graph_problems.h
//
// Classical graph problems expressed using the DAG and Polytree containers.
// All functions are free functions — no new types introduced.

#include <graph/static_dag.h>
#include <graph/static_dag_algo.h>
#include <graph/static_polytree.h>
#include <graph/static_polytree_algo.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace wz::core::graph {


    // ─── Reachability ─────────────────────────────────────────────────────────────
    //
    // Can 'target' be reached from 'start'?
    // Uses sink-based DFS with early termination — the traversal aborts
    // the moment 'target' is found, without visiting remaining nodes.

    namespace detail {

        struct ReachabilitySink {
            NodeHandle target;
            bool       found = false;

            bool push(NodeHandle n) {
                if (n == target) { found = true; return false; } // abort
                return true;
            }
        };

    } // namespace detail

    template<typename N, typename E>
    bool is_reachable(const DAG<N, E>& g, NodeHandle start, NodeHandle target)
    {
        if (start == target) return true;
        detail::ReachabilitySink sink{ target };
        dfs(g, start, sink);
        return sink.found;
    }

    template<typename N, typename E>
    bool is_reachable(const Polytree<N, E>& t, NodeHandle start, NodeHandle target)
    {
        if (start == target) return true;
        detail::ReachabilitySink sink{ target };
        dfs(t, start, sink);
        return sink.found;
    }


    // ─── Critical path ────────────────────────────────────────────────────────────
    //
    // Longest weighted path from any root to any leaf in a DAG.
    // Edge weights are extracted via a user-supplied function:
    //   float weight_fn(NodeHandle from, NodeHandle to, const E& edge_data)
    //
    // Runs in topo order — O(V+E).
    // Returns the cost and the path as a vector of NodeHandles.
    //
    // EdgeWeight must be a callable: (NodeHandle, NodeHandle, const E&) -> float

    struct CriticalPathResult {
        float                   cost = 0.f;
        std::vector<NodeHandle> path;
    };

    template<typename N, typename E, typename EdgeWeight>
    CriticalPathResult critical_path(const DAG<N, E>& g, EdgeWeight&& weight_fn)
    {
        const uint32_t nc = node_count(g);
        if (nc == 0) return {};

        constexpr float NEG_INF = std::numeric_limits<float>::lowest();

        std::vector<float>      dist(nc, NEG_INF);
        std::vector<NodeHandle> pred(nc, INVALID_NODE);

        // Roots start at zero cost
        for (uint32_t i = 0; i < nc; ++i)
            if (is_root(g, i)) dist[i] = 0.f;

        // Relax edges in topo order
        for (NodeHandle u : topo_order(g)) {
            if (dist[u] == NEG_INF) continue;
            auto nbrs = children(g, u);
            auto edata = outgoing_edge_data(g, u);
            for (uint32_t i = 0; i < nbrs.size(); ++i) {
                NodeHandle v = nbrs[i];
                float      w = weight_fn(u, v, edata[i]);
                float      nd = dist[u] + w;
                if (nd > dist[v]) {
                    dist[v] = nd;
                    pred[v] = u;
                }
            }
        }

        // Find the leaf with maximum distance
        NodeHandle best_leaf = INVALID_NODE;
        float      best_cost = NEG_INF;
        for (uint32_t i = 0; i < nc; ++i) {
            if (is_leaf(g, i) && dist[i] > best_cost) {
                best_cost = dist[i];
                best_leaf = i;
            }
        }

        if (best_leaf == INVALID_NODE) return {};

        // Reconstruct path by following predecessors
        CriticalPathResult result;
        result.cost = best_cost;
        for (NodeHandle cur = best_leaf; cur != INVALID_NODE; cur = pred[cur])
            result.path.push_back(cur);
        std::reverse(result.path.begin(), result.path.end());
        return result;
    }


    // ─── Lowest common ancestor (Polytree only) ───────────────────────────────────
    //
    // The LCA is the deepest node that is an ancestor of both a and b.
    // Only well-defined on a polytree — each node has exactly one ancestor chain.
    //
    // Strategy: materialize ancestors of 'a' into a scratch buffer,
    // then walk ancestors of 'b' until one appears in that set.
    // O(depth_a + depth_b) time, O(depth_a) scratch space.

    template<typename N, typename E>
    std::optional<NodeHandle> lowest_common_ancestor(
        const Polytree<N, E>& t,
        NodeHandle           a,
        NodeHandle           b)
    {
        if (a == b) return a;

        // Collect full ancestor chain of 'a' including 'a' itself
        std::vector<NodeHandle> anc_a;
        anc_a.push_back(a);
        NodeHandle cur = parent(t, a);
        while (cur != INVALID_NODE) {
            anc_a.push_back(cur);
            cur = parent(t, cur);
        }

        // Walk ancestors of 'b', return first match
        if (std::find(anc_a.begin(), anc_a.end(), b) != anc_a.end()) return b;

        cur = parent(t, b);
        while (cur != INVALID_NODE) {
            if (std::find(anc_a.begin(), anc_a.end(), cur) != anc_a.end())
                return cur;
            cur = parent(t, cur);
        }

        return std::nullopt; // disconnected — no common ancestor
    }


    // ─── Depth assignment ─────────────────────────────────────────────────────────
    //
    // Assigns each node its depth from the nearest root (roots = depth 0).
    // Runs in topo order — O(V).
    // Caller provides the output buffer (size must be >= node_count).

    template<typename N, typename E>
    void compute_depths(const DAG<N, E>& g, std::span<uint32_t> out)
    {
        assert(out.size() >= node_count(g));
        std::fill(out.begin(), out.end(), 0u);
        for (NodeHandle u : topo_order(g))
            for (NodeHandle v : children(g, u))
                out[v] = std::max(out[v], out[u] + 1);
    }

    template<typename N, typename E>
    void compute_depths(const Polytree<N, E>& t, std::span<uint32_t> out)
    {
        assert(out.size() >= node_count(t));
        std::fill(out.begin(), out.end(), 0u);
        for (NodeHandle u : topo_order(t))
            for (NodeHandle v : children(t, u))
                out[v] = out[u] + 1; // no max needed — single parent
    }


} // namespace wz::core::graph