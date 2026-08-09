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

    namespace detail {

        // ─── #293 substitution seam — collect ────────────────────────────────
        //
        // Serial post-order recursion over one subtree: recurse into each child
        // (in children(t, n) == CSR child-index order), THEN emit this node's
        // up-message. Post-order guarantees every child's up-message is written
        // before the parent folds them, and the recursion order matches the fold
        // order the message kernel uses (for_each_neighbor_except walks the same
        // CSR order) — so results are independent of how the child subtrees are
        // scheduled. This is the ONE function #293 replaces: it will dispatch each
        // child subtree via wz::tasks::run and nest-wait before the post-order
        // emit, leaving the `collect` kernel and the child-index order untouched.
        template<typename N, typename E, typename CollectFn>
        void bp_collect_subtree(
            SharedEdgePolytree<N, E>& t, NodeHandle n, CollectFn& collect) {
            for (const NodeHandle c : children(t, n))
                bp_collect_subtree(t, c, collect);  // #293: run(child subtree) / wait
            const NodeHandle p = parent(t, n);
            if (p != INVALID_NODE)                  // a root sends no up-message
                collect(n, p, edge_to_parent(t, n));
        }

        // ─── #293 substitution seam — distribute ─────────────────────────────
        //
        // Serial pre-order recursion over one subtree: emit this node's down-
        // message to every child (in children(t, n) == CSR child-index order),
        // THEN recurse into the child subtrees. Pre-order guarantees a child's
        // incoming down-message is written before that child emits its own. The
        // emit pass and the recursion pass are kept separate so the recursion pass
        // is a clean fan-out over independent subtrees. This is the ONE function
        // #293 replaces: after the serial pre-order emit it will dispatch the child
        // subtrees via wz::tasks::run / wait, leaving the `distribute` kernel and
        // the child-index order untouched.
        template<typename N, typename E, typename DistributeFn>
        void bp_distribute_subtree(
            SharedEdgePolytree<N, E>& t, NodeHandle n, DistributeFn& distribute) {
            for (const NodeHandle c : children(t, n))
                distribute(n, c, edge_to_child(t, n, c));
            for (const NodeHandle c : children(t, n))
                bp_distribute_subtree(t, c, distribute);  // #293: run(child subtree) / wait
        }

    } // namespace detail

    // Collect sweep (LEAF -> ROOT). Invokes collect(node, parent, EdgeData&
    // edge_to_parent) for every non-root node so the caller writes `node`'s
    // message TOWARD its parent. Driven as a post-order traversal from each root
    // of the forest: every child of `node` has already written its up-message when
    // `node` is visited, so the caller can combine them via
    // for_each_neighbor_except(t, node, parent, ...). (Formerly a flat reverse-
    // topo loop; reshaped into dependency-explicit recursion so #293 can drop a
    // parallel run/wait driver behind detail::bp_collect_subtree without touching
    // this signature or the message kernel.)
    template<typename N, typename E, typename CollectFn>
    void bp_collect(SharedEdgePolytree<N, E>& t, CollectFn&& collect) {
        for_each_root(t, [&](NodeHandle root) {
            detail::bp_collect_subtree(t, root, collect);
        });
    }

    // Distribute sweep (ROOT -> LEAF). Invokes distribute(node, child, EdgeData&
    // edge_to_child) for every (node, child) pair so the caller writes `node`'s
    // message TOWARD that child. Driven as a pre-order traversal from each root of
    // the forest: `node`'s parent has already written its down-message to `node`
    // when `node` is visited, so the caller can combine it with the up-messages of
    // `node`'s OTHER children via for_each_neighbor_except(t, node, child, ...).
    // (Formerly a flat forward-topo loop; reshaped into dependency-explicit
    // recursion so #293 can drop a parallel run/wait driver behind
    // detail::bp_distribute_subtree without touching this signature or the kernel.)
    template<typename N, typename E, typename DistributeFn>
    void bp_distribute(SharedEdgePolytree<N, E>& t, DistributeFn&& distribute) {
        for_each_root(t, [&](NodeHandle root) {
            detail::bp_distribute_subtree(t, root, distribute);
        });
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
