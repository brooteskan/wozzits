#pragma once

// engine/cognition/tree_bp.h
//
// Exact tree belief propagation with QUANTUM TENSOR messages -- the contraction
// architecture that finally puts the bp_collect/bp_distribute scaffold to work,
// and the bridge toward the chi-truncated tree tensor network (this layer minus
// the SVD truncation).
//
// Each node holds a small LOCAL tensor (not the whole joint state); a message
// along a bond is the contracted ENVIRONMENT of one subtree (a chi x chi matrix
// in the norm network). The two scaffold sweeps fill the messages -- bp_collect
// (leaf->root) the "up" right-environments, bp_distribute (root->leaf) the "down"
// left-environments -- and each node's reduced state is then its local tensor
// contracted with its incoming environments. On a TREE this is EXACT (no loops =
// no BP error) and costs linear in node count, with the cost in the bond
// dimension rather than 2^N.
//
// FIRST CUT: single-qubit sites on a CHAIN (an MPS -- the simplest tree). The
// scaffold supports branching topologically; branching tree tensors (a node with
// >2 bonds) are the follow-up, as is chi-truncation (the SVD) for the scalable
// TTN.

#include <graph/shared_edge_polytree.h>
#include <engine/qstate/qstate.h>

#include <cstdint>
#include <vector>

namespace wz::cognition
{
    // A single-qubit MPS site tensor A[s][l][r]: s in {0,1} is the physical leg,
    // l in [0,left) the left (parent-side) bond, r in [0,right) the right
    // (child-side) bond. Flat layout: a[(s*left + l)*right + r].
    struct MpsSite
    {
        uint32_t left = 1;
        uint32_t right = 1;
        std::vector<wz::qstate::Complex> a;  // size 2 * left * right
    };

    // Per-bond environment messages (the shared edge slot). up = right-
    // environment (child -> parent), down = left-environment (parent -> child);
    // each a dim x dim matrix (row-major [ket*dim + bra]).
    struct BondEnv
    {
        uint32_t dim = 1;
        std::vector<wz::qstate::Complex> up;
        std::vector<wz::qstate::Complex> down;
    };

    using TreeBpNetwork =
        wz::core::graph::SharedEdgePolytree<MpsSite, BondEnv>;

    // Each node's <sigma_z> by exact tree belief propagation (two scaffold sweeps
    // + tensor messages). No SVD; exact on a tree.
    std::vector<double> tree_bp_sigma_z(TreeBpNetwork& net);

    // Dense reference: contract the chain MPS into the full 2^N statevector and
    // read each site's <sigma_z> directly. For tests / small chains only.
    std::vector<double> dense_sigma_z(const TreeBpNetwork& net);

    // Two-site update (TEBD "simple update"): apply a two-qubit gate across the
    // bond shared by adjacent chain sites `left` (its right bond) and `right` (its
    // left bond), then SVD-truncate that bond to at most `chi`. Both site tensors
    // are rewritten in place; the new bond holds the chi largest Schmidt values,
    // so the result is the best rank-chi approximation of the gated two-site
    // state. This is the step that turns the exact tree-BP contraction into the
    // SCALABLE chi-truncated tree tensor network: couplings are applied as gates
    // and the entanglement they create is capped at chi.
    //
    // `gate` is the 4x4 matrix of the two-qubit gate, row-major and indexed
    // [out * 4 + in] with out = s_left'*2 + s_right', in = s_left*2 + s_right.
    void apply_two_site_gate(
        MpsSite& left,
        MpsSite& right,
        const std::vector<wz::qstate::Complex>& gate,
        uint32_t chi);

    // Apply a single-qubit gate to a site's physical leg (no bond change, no
    // truncation): A'[s'][l][r] = sum_s gate[s'*2 + s] A[s][l][r]. `gate` is the
    // 2x2 matrix row-major [out*2 + in].
    void apply_one_site_gate(
        MpsSite& site, const std::vector<wz::qstate::Complex>& gate);
}
