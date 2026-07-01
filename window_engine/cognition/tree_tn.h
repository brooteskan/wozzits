#pragma once

// cognition/tree_tn.h
//
// Exact tree belief propagation on a GENERAL tree (branching), generalizing the
// chain/MPS contraction in tree_bp.h. A node now has one physical qubit, a parent
// bond, and ANY number of child bonds -- so a star (a group node bonded to all
// its members) or a deeper hierarchy contracts, not just a chain. This is what
// lets the cognition group tree (fireteam -> squad -> ...) be contracted exactly.
//
// The bp_collect/bp_distribute scaffold drives the same two sweeps; the messages
// (per-bond chi x chi environment matrices) are the same BondEnv. Only the
// per-node contraction generalizes: it sums over the physical leg, the parent
// bond, and every child bond, folding in each child's up-message. Exact on a
// tree, no truncation. (chi-truncated evolution on branching trees is a
// follow-up; this slice is the exact contraction.)

#include <cognition/tree_bp.h>  // BondEnv
#include <graph/shared_edge_polytree.h>
#include <cognition/qstate/qstate.h>

#include <cstdint>
#include <vector>

namespace wz::engine::cognition
{
    // A general tree-tensor-network node: one physical qubit (dim 2), a parent
    // bond (dim 1 for a root), and child_bonds.size() child bonds. The child
    // bonds MUST be ordered to match children(net, node). Tensor layout is
    // row-major over dims [2, parent_bond, child_bonds[0], child_bonds[1], ...].
    struct TreeNode
    {
        uint32_t parent_bond = 1;
        std::vector<uint32_t> child_bonds;
        std::vector<wz::engine::cognition::qstate::Complex> t;
    };

    using TreeTnNetwork = wz::core::graph::SharedEdgePolytree<TreeNode, BondEnv>;

    // Each node's <sigma_z> by exact tree belief propagation over the general
    // tree. No truncation; exact on a tree.
    std::vector<double> tree_tn_sigma_z(TreeTnNetwork& net);
}
