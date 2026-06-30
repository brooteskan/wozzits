#pragma once

// wz/core/graph/shared_edge_polytree_algo.h
//
// Generic algorithms over SharedEdgePolytree. Following the graph convention,
// the base header (shared_edge_polytree.h) holds the structure + primitive
// operations + simple traversals, and this companion holds the richer, derived
// algorithms -- here, the two-sweep EXACT belief-propagation scaffold.
//
// This layer is DOMAIN-AGNOSTIC: it knows nothing about quantum states. It
// drives the message-passing sweeps in the order that makes belief propagation
// exact on a tree, plus the exclude-the-target neighbor access; the CALLER
// supplies the message algebra (how a node's outgoing message is built from the
// incoming messages of its other neighbors). The wavefunction-specific
// instantiation -- the U_ZZ bond contraction, chi-truncation, and
// M_e . M_reverse normalization -- lives in the cognition layer and parameterizes
// these sweeps; it does NOT belong here.

#include <graph/shared_edge_polytree.h>

#include <cstdint>

namespace wz::core::graph {

    // ─── Two-sweep belief propagation ─────────────────────────────────────────────
    //
    // Messages live in the EdgeData slot (edge_store[child]) -- the caller's
    // EdgeData typically holds one message per direction along the bond ("up" =
    // child->parent, "down" = parent->child). The scaffold never reads or writes
    // those fields; it only sequences the sweeps and hands the caller the shared
    // slot to fill. Because edge_store[c] is a single shared slot, a message the
    // collect sweep writes is the same object the distribute sweep and the
    // neighbors read -- no copies, no staleness.

    // Collect sweep (LEAF -> ROOT). Visits every non-root node in reverse
    // topological order and invokes collect(node, parent, EdgeData& edge_to_parent)
    // so the caller writes `node`'s message TOWARD its parent. By the visit order,
    // every child of `node` has already written its up-message, so the caller can
    // combine them via for_each_neighbor_except(t, node, parent, ...).
    template<typename N, typename E, typename CollectFn>
    void bp_collect(SharedEdgePolytree<N, E>& t, CollectFn&& collect) {
        auto order = topo_order(t);  // parents precede children
        for (uint32_t i = static_cast<uint32_t>(order.size()); i-- > 0;) {
            const NodeHandle n = order[i];
            const NodeHandle p = parent(t, n);
            if (p == INVALID_NODE) continue;  // a root sends no up-message
            collect(n, p, edge_to_parent(t, n));
        }
    }

    // Distribute sweep (ROOT -> LEAF). Visits every (node, child) pair in
    // topological order and invokes distribute(node, child, EdgeData& edge_to_child)
    // so the caller writes `node`'s message TOWARD that child. By the visit order,
    // `node`'s parent has already written its down-message to `node`, so the caller
    // can combine it with the up-messages of `node`'s OTHER children via
    // for_each_neighbor_except(t, node, child, ...).
    template<typename N, typename E, typename DistributeFn>
    void bp_distribute(SharedEdgePolytree<N, E>& t, DistributeFn&& distribute) {
        auto order = topo_order(t);  // root -> leaf
        for (const NodeHandle n : order) {
            for (const NodeHandle c : children(t, n)) {
                distribute(n, c, edge_to_child(t, n, c));
            }
        }
    }

    // Both sweeps. After this, every directed edge holds its converged message and
    // each node's belief is the caller's combination of all incoming messages with
    // the node's local factor (computed by the caller, e.g. via for_each_neighbor).
    // On a TREE this is EXACT in these two passes -- no iteration to convergence.
    template<typename N, typename E, typename CollectFn, typename DistributeFn>
    void belief_propagation(
        SharedEdgePolytree<N, E>& t, CollectFn&& collect, DistributeFn&& distribute) {
        bp_collect(t, static_cast<CollectFn&&>(collect));
        bp_distribute(t, static_cast<DistributeFn&&>(distribute));
    }

} // namespace wz::core::graph
