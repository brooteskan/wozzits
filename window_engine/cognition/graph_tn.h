#pragma once

// cognition/graph_tn.h
//
// Finite-chi ENTANGLED coordination backend on a CYCLIC graph -- the general-
// graph generalization of the chain MPS (tree_bp / apply_two_site_gate) to an
// arbitrary undirected relationship graph, evolved by the standard Vidal /
// "simple update" TEBD algorithm.
//
// Where exact_group holds the WHOLE joint wavefunction (chi = infinity, cost
// exponential in group size) and mean_field / chi = 1 loopy BP factorize
// completely (no entanglement across a bond), this backend keeps a bounded-rank
// (chi) entangled state per bond: each node carries a small tensor with one leg
// per incident edge, and each edge carries a Vidal lambda (the Schmidt vector of
// that cut). A coupling is applied as a two-site gate on the shared edge, and the
// entanglement it creates is truncated back to chi via the SVD -- the same
// tree_bp simple-update primitive, generalized to fold in the MEAN-FIELD
// environment (the neighboring lambdas) so it is well-posed on a graph with
// loops. On a TREE this is exact; on a loopy graph it is the Vidal/BP
// approximation, exact in the chi -> full limit.
//
// Cost is polynomial in chi and linear in edges, so this backend adds SIZE at
// bounded fidelity where exact_group cannot. It exposes the same read surface
// (decision_z / decisions / collapse) as the other backends so it is
// interchangeable behind the coordination contract. Single-qubit agents in this
// first cut: agent i owns one physical leg (dim 2) of its node tensor.
//
// Sign conventions match exact_group exactly: goal field > 0 favors +z (|0>);
// bond j > 0 ferromagnetic (agree), j < 0 anti (disagree); expectation_z(|0>) =
// +1, (|1>) = -1.

#include <cognition/exact_group.h>  // ExactBond, Goal
#include <cognition/qstate/qstate.h>
#include <graph/shared_edge_graph.h>

#include <cstdint>
#include <vector>

namespace wz::engine::cognition
{
    // A general-graph tensor-network node. Tensor layout is row-major over dims
    // [physical(2), bond_0, bond_1, ..., bond_{degree-1}] -- mode 0 is always the
    // physical qubit; the bond legs are ordered to match the iteration order of
    // for_each_neighbor(graph, u) so a leg's position IS its neighbor's position.
    // Each bond leg dim (bond_dim[k]) is <= chi.
    struct GraphSite
    {
        uint32_t degree = 0;
        std::vector<uint32_t> bond_dim;  // size degree; leg k's dim
        std::vector<wz::engine::cognition::qstate::Complex> t;  // 2 * prod(bond_dim)
    };

    // The shared edge slot: the coupling and the Vidal lambda living ON the cut.
    // Both endpoints see the same lambda (it is the Schmidt spectrum of the bond),
    // entries descending >= 0, lambda.size() == the current bond dim.
    struct GraphBond
    {
        double j = 0.0;
        std::vector<double> lambda;  // Schmidt / Vidal spectrum of this cut
    };

    struct GraphTn
    {
        wz::core::graph::SharedEdgeGraph<GraphSite, GraphBond> graph;
        std::vector<double> goal_field;   // per-agent longitudinal field h
        uint32_t chi = 4;                 // bond-dimension cap
        // Sum of the per-edge RELATIVE truncation errors from the most recent
        // relax_step (telemetry only; 0 when chi >= every full bond). Reset at the
        // top of each relax_step.
        double last_truncation_error = 0.0;
    };

    // Build a fresh graph-TN over `agent_count` agents from a raw bond list.
    // Bonds are canonicalized (canonicalize_bonds: self-bonds dropped, parallels
    // summed, girth >= 3) before the graph is built. Every node starts in the
    // product |+> state (unpolarized physical marginal) with all bond legs dim 1;
    // every edge's lambda starts {1.0}; goal_field starts all zero.
    GraphTn make_graph_tn(
        uint32_t agent_count, const std::vector<ExactBond>& bonds, uint32_t chi);

    // Set the per-agent goal field = the sum of the given goals' fields (each
    // agent's goals add). Replaces any previously set goal field. Same semantics
    // as exact_group::set_goals.
    void set_goals(GraphTn& g, const std::vector<Goal>& goals);

    // One imaginary-time Trotter step toward the group's entangled ground state.
    // 2nd-order symmetric (Strang) split matching exact_group / ttn: half a step
    // of the single-site fields, a full step of the two-site couplings (each an
    // imaginary-time ZZ gate applied via the Vidal simple-update primitive), then
    // the other half of the fields. Accumulates the per-edge relative truncation
    // errors into last_truncation_error.
    void relax_step(GraphTn& g, double gamma, double dtau);
    void relax(GraphTn& g, double gamma, double dtau, uint32_t iterations);

    // Agent `agent`'s decision marginal: <sigma_z> in [-1, 1], read from the
    // lambda-gauge (Vidal/BP) reduced density matrix -- exact on a tree. Same read
    // surface as the exact / mean-field backends.
    double decision_z(const GraphTn& g, uint32_t agent);

    // Every agent's decision marginal in one pass.
    std::vector<double> decisions(const GraphTn& g);

    // Collapse agent `agent`'s physical leg onto `bit` (project its tensor onto
    // that outcome, zeroing the opposite physical component). The lambda-gauge
    // marginal renormalizes by trace, so no explicit renormalization is needed.
    void collapse(GraphTn& g, uint32_t agent, bool bit);
}
