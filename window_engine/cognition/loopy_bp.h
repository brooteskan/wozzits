#pragma once

// cognition/loopy_bp.h
//
// chi = 1 LOOPY belief propagation / self-consistent mean field on a CYCLIC
// coordination graph (the loopy analog of mean_field.h, which is the acyclic /
// tree chi = 1 backend). Each agent is a single-qubit qstate register; agents
// coordinate through an effective longitudinal field h_eff built from their
// neighbors' current (DAMPED) <sigma_z> broadcasts. It is SVD-free and linear
// per sweep in the number of edges.
//
// This is the loopy sibling of mean_field on wz::core::graph::SharedEdgeGraph
// (the general undirected graph with cycles). The one thing cycles force that
// trees do not: on a FRUSTRATED loop (e.g. an all-antiferromagnetic triangle)
// the naive Jacobi mean-field fixed-point iteration OSCILLATES and never
// settles. We tame that with message DAMPING -- each broadcast z is a convex
// mix of its previous value and the freshly relaxed marginal, so the update is
// contractive enough to converge to a compromise fixed point on the loop.
//
// IMPORTANT -- this is NOT exact:
//   * chi = 1: it is a product-state (mean-field) approximation and carries NO
//     genuine entanglement between agents. It is not exact even on a quantum
//     TREE (that is the finite-chi tree tensor network, mean_field.h's deferred
//     upgrade), let alone on a loop.
//   * On loops it is the classic loopy-BP self-consistent mean field: it
//     reproduces the QUALITATIVE frustration signatures of the exact village
//     oracle (tests/cognition/village_tests.cpp -- even cycles satisfy cleanly,
//     odd cycles frustrate) but NOT the exact numbers (the triangle's clean
//     -1/3 connected correlation, the cat-state <sigma_z> = 0). A QUANTITATIVE
//     match is a finite-chi (Seam 3) goal.
//   * A proper CAVITY / exclude-the-target belief propagation (each outgoing
//     message excludes the incoming one, with the M * M_reverse normalization)
//     is the correct BP refinement over this broadcast-everything mean field --
//     it too belongs to the finite-chi tier. Here every neighbor sees the same
//     single broadcast z per agent (a self-consistent mean field, not true BP).
//
// Sign conventions match exact_group / mean_field: a goal field > 0 favors +z
// (|0>); a ferromagnetic bond j > 0 pulls neighbors to AGREE; an anti bond
// j < 0 pulls them to DISAGREE. expectation_z(|0>) = +1, expectation_z(|1>) =
// -1.
//
// Single-qubit agents in this first cut: agent i's coupled qubit is qubit 0 of
// its own register (qstate::uniform(1)).

#include <cognition/exact_group.h>  // ExactBond, Goal (shared API types)
#include <cognition/qstate/qstate.h>
#include <graph/shared_edge_graph.h>

#include <cstdint>
#include <vector>

namespace wz::engine::cognition
{
    // Coupling carried on an undirected edge of the loopy graph. No per-edge
    // message is stored here -- the loopy messages are the per-AGENT damped z
    // broadcasts (LoopyBpGroup::z), which every incident edge reads. (Cavity /
    // per-edge directed messages are the finite-chi BP refinement.)
    struct LoopyBond
    {
        double j = 0.0;  // > 0 ferromagnetic (agree), < 0 anti (disagree)
    };

    struct LoopyBpGroup
    {
        // One single-qubit register per agent; edges carry the couplings.
        wz::core::graph::SharedEdgeGraph<
            wz::engine::cognition::qstate::Register, LoopyBond> graph;

        std::vector<double> goal_field;  // per-agent longitudinal h (from Goals)
        std::vector<double> z;           // per-agent DAMPED broadcast <sigma_z>
                                         //   -- the persistent loopy messages
        std::vector<bool>   clamped;     // collapsed agents: skipped in relax,
                                         //   z pinned to the committed value

        // Damping lambda in (0, 1]: z_new = (1 - lambda) * z_old + lambda *
        // <sigma_z>. Lower = more damping (more of the old broadcast retained),
        // which tames frustrated-loop oscillation. lambda = 1 is undamped (pure
        // Jacobi -- oscillates on frustrated loops).
        double damping = 0.5;
    };

    // Build the group: single-qubit uniform registers, one per agent; undirected
    // edges from the CANONICALIZED bonds (self-bonds dropped, parallel edges on
    // the same unordered pair summed, matching the exact backend's edge model);
    // z all 0, clamped all false.
    LoopyBpGroup make_loopy_bp_group(
        uint32_t agent_count,
        const std::vector<ExactBond>& bonds,
        double damping = 0.5);

    // Set the per-agent goal field = the sum of the given goals' fields (each
    // agent's goals add). Replaces any previously set goal field. Matches
    // exact_group::set_goals semantics.
    void set_goals(LoopyBpGroup& g, const std::vector<Goal>& goals);

    // One damped Jacobi loopy sweep:
    //   1. Compute h_eff for every agent from the CURRENT (previous-step) damped
    //      z: h_eff[u] = goal_field[u] + sum over neighbors of e.j * z[nbr].
    //   2. Relax each UNCLAMPED agent's register toward its ground state under
    //      (gamma, h_eff[u]) via one imaginary-time step of size dtau.
    //   3. Update z with damping (clamped agents keep their pinned z).
    // All h_eff are computed BEFORE any register is relaxed (true Jacobi: every
    // agent reads the previous step's broadcasts).
    void relax_step(LoopyBpGroup& g, double gamma, double dtau);

    // Run `iterations` relax steps. Does NOT reset z between calls -- the
    // persistent z across ticks IS the warm-start (a settled loop stays settled;
    // a slowly-annealed gamma re-relaxes from where it left off).
    void relax(LoopyBpGroup& g, double gamma, double dtau, uint32_t iterations);

    // Agent's decision marginal <sigma_z> in [-1, 1] -- the TRUE per-agent
    // marginal read straight off its register (NOT the damped broadcast z, which
    // lags). Same read surface as the exact / mean-field backends.
    double decision_z(const LoopyBpGroup& g, uint32_t agent);

    // Every agent's decision marginal in one pass.
    std::vector<double> decisions(const LoopyBpGroup& g);

    // Collapse agent `agent`'s qubit onto `bit`: project its register, clamp it
    // (skipped by subsequent relax_step), and PIN its broadcast z to the
    // committed value (bit == true -> |1> -> z = -1; bit == false -> |0> ->
    // z = +1). A clamped agent stays fixed and its neighbors feel it as a fixed
    // field. Clamps clear only on a fresh make_loopy_bp_group (the store's
    // rearm/reshape rebuild).
    void collapse(LoopyBpGroup& g, uint32_t agent, bool bit);

    // Measure agent `agent` along axis theta -- a purely LOCAL rotated measurement
    // of the agent's own single-qubit register. The outcome is exact for that
    // qubit, but a chi = 1 product state carries NO entanglement, so neighbors are
    // conditioned only CLASSICALLY (the agent is clamped + its broadcast z pinned,
    // felt as a fixed field on the next relax). So it faithfully reports the
    // CLASSICAL floor -- it cannot violate a Bell inequality at any angles.
    // Returns the outcome bit.
    bool measure_in_basis(
        LoopyBpGroup& g, uint32_t agent, double theta,
        wz::engine::cognition::qstate::Rng& rng);
}
