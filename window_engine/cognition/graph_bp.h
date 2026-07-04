#pragma once

// cognition/graph_bp.h
//
// Loopy BELIEF-PROPAGATION readout for the finite-chi graph tensor network
// (graph_tn). A pure, ADDITIVE analysis layer: it reads a const GraphTn and
// computes each agent's decision marginal by iterating BP messages to a fixed
// point on the CYCLIC norm network -- the loopy generalization of the exact tree
// contraction in tree_tn.cpp (up_message / down_message_to / marginal_sigma_z).
//
// graph_tn already ships a lambda-gauge (Vidal/BP) marginal (decision_z): it
// reads each node against its LOCAL environment (diag(lambda^2) per incident
// bond). That local gauge is exact on a tree but ignores the loop correlations a
// cyclic graph carries. This layer instead ITERATES the environment messages
// around the loops to a self-consistent fixed point before reading, which is the
// same information a proper loop-BP marginal uses. On a TREE the two agree
// exactly (both are exact); on a loop they differ by the BP loop error.
//
// The norm network is the DOUBLE LAYER: each node u contributes T_u (ket) and
// conj(T_u) (bra) with their physical legs tied together (same s). A BP message
// on a directed edge u -> v is a D x D complex matrix over that bond's index
// (row-major [ket*D + bra], D = the bond's current dim), representing the
// environment on the u-v bond as seen from u's side -- exactly a BondEnv up/down
// matrix (tree_bp.h), only now solved by iteration rather than two sweeps.
//
// SCOPE (honest): 3b-i gives (1) the message infrastructure, (2) a BP marginal
// readout that is EXACT on trees and matches graph_tn's lambda-gauge readout
// there, and (3) the epsilon_BP diagnostic (a well-defined finite discrepancy
// number). It does NOT by itself fix the low-gamma frustrated-loop over-
// polarization: that error lives in the EVOLVED STATE (simple update's local-
// lambda truncation), not only in the readout gauge. The quantitative fix is the
// SEPARATE next phase 3b-ii, which feeds these converged messages back as the
// truncation environment inside apply_edge_gate. This file does NOT touch
// graph_tn or the evolution path.
//
// Sign conventions are graph_tn's: expectation_z(|0>) = +1, (|1>) = -1.

#include <cognition/graph_tn.h>
#include <cognition/qstate/qstate.h>

#include <cstdint>
#include <vector>

namespace wz::engine::cognition
{
    // The BP message set for one GraphTn: two D x D matrices per undirected edge
    // (both directions of the bond). Indexed by edge id (the edge_store index in
    // the graph). forward[e] is the a -> b message (a = the LOW endpoint, matching
    // canonicalize_bonds' a < b), backward[e] the b -> a message. Each matrix is
    // dim[e] x dim[e], row-major [ket*dim + bra] over that bond's index -- the
    // same representation as tree_bp's BondEnv up/down.
    struct BpMessages
    {
        std::vector<std::vector<wz::engine::cognition::qstate::Complex>> forward;
        std::vector<std::vector<wz::engine::cognition::qstate::Complex>> backward;
        std::vector<uint32_t> dim;  // size edge_count; message is dim[e] x dim[e]
    };

    // How converge_bp finished, for the tests (which assert convergence and, on a
    // frustrated loop, report the residual with vs without damping).
    struct BpConvergence
    {
        uint32_t iterations = 0;  // sweeps performed (<= max_iters)
        double residual = 0.0;    // max abs message-element change on the LAST sweep
        bool converged = false;   // residual < tol at return
    };

    // Iterate BP messages on the cyclic norm network to a fixed point.
    //
    // Every message is initialized to the normalized identity (I / D over that
    // bond's current dim). Each sweep is a JACOBI update: all new directed messages
    // are computed from the PREVIOUS sweep's messages, then swapped in (order-
    // independent / deterministic). A message u -> v is recomputed by folding T_u
    // with conj(T_u), absorbing every INCOMING message m_{w->u} for neighbors
    // w != v into u's other bond legs, contracting the physical leg and all non-
    // target bond legs, leaving the target bond's (ket, bra) legs open -- exactly
    // down_message_to's contraction with "parent_down + other children" replaced by
    // "all incident messages except the target edge's".
    //
    // DAMPING is applied per element BEFORE normalization: msg_new =
    // (1 - damping) * msg_old + damping * computed. Then each message is divided by
    // its TRACE so trace == 1 (these environment matrices are PSD with positive
    // trace; trace-normalizing keeps them bounded -- the loop-stable analog of
    // tree_bp leaving them unnormalized). Damping tames the frustrated-loop limit
    // cycle; pure Jacobi (damping = 1) can oscillate on a frustrated loop.
    //
    // Stops when the max abs element change over a sweep < tol, or at max_iters.
    BpMessages converge_bp(
        const GraphTn& g, uint32_t max_iters, double tol, double damping,
        BpConvergence* out = nullptr);

    // Agent `agent`'s decision marginal <sigma_z> in [-1, 1] from the converged
    // messages: fold EVERY incoming message m_{w->u} into T_u, contract ket vs
    // conj(T_u) over all bond legs leaving the physical leg open -> 2x2 diagonal
    // rho -> (rho00 - rho11) / (rho00 + rho11). This is marginal_sigma_z on the
    // graph. On a tree it equals both exact and graph_tn's lambda-gauge decision_z.
    double bp_marginal_z(
        const GraphTn& g, const BpMessages& msgs, uint32_t agent);

    // Every agent's BP decision marginal from an already-converged message set.
    std::vector<double> bp_decisions(const GraphTn& g, const BpMessages& msgs);

    // Ergonomic path: converge, then read every marginal. Defaults chosen for the
    // tree-exactness tests (tight tol, moderate damping); a caller wanting the
    // convergence stats runs converge_bp + bp_decisions itself.
    std::vector<double> bp_decisions_converged(
        const GraphTn& g, uint32_t max_iters = 200, double tol = 1e-11,
        double damping = 0.5);
}
