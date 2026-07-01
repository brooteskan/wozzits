#pragma once

// cognition/exact_group.h
//
// Entangled coordination backend -- the chi = infinity (EXACT) end of the
// coordination dial, and the genuine-entanglement counterpart to the chi = 1
// mean_field backend (mean_field.h). A small group's JOINT wavefunction is held
// as ONE qstate register over all its agents' qubits, and the couplings act
// directly on that joint state, so it carries genuine, non-factorizable,
// shared-fate entanglement -- the ferromagnetic ground state is the cat state
// (|0..0> + |1..1>)/sqrt2: every agent unpolarized (<sigma_z> = 0) yet perfectly
// correlated. A product (mean-field) state cannot represent that.
//
// Cost is exponential in group size, so this backend is for SMALL groups (a
// fireteam). The chi-truncated tree tensor network is the separate SCALING layer
// (it adds size, not fidelity) and is the deferred follow-up. Both backends
// expose the same read surface (decision_z) so they are interchangeable behind
// the coordination contract.
//
// Single-qubit agents in this first cut: agent i owns qubit i of the joint
// register.

#include <cognition/qstate/qstate.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace wz::engine::cognition
{
    struct ExactBond
    {
        uint32_t a = 0;
        uint32_t b = 0;
        double j = 0.0;  // > 0 ferromagnetic (agree), < 0 anti
    };

    // A goal biases an agent's decision: a longitudinal field that lowers the
    // energy of one disposition. field > 0 favors +z (|0>), field < 0 favors -z
    // (|1>); the magnitude is the goal's importance. Multiple goals on an agent
    // sum. Goals enter the same Hamiltonian the group anneals against, so pursuit
    // is just the relaxation -- and goals that conflict with the couplings (or
    // each other) produce frustration: no clean ground state, so the agents waver
    // / compromise rather than satisfy everything.
    struct Goal
    {
        uint32_t agent = 0;
        double field = 0.0;
    };

    struct ExactGroup
    {
        wz::engine::cognition::qstate::Register joint;     // over `agent_count` qubits
        std::vector<ExactBond> bonds;
        std::vector<double> goal_field;  // per-agent longitudinal goal bias
    };

    // Joint register starts in equal superposition over all agents; goal_field
    // starts all zero.
    ExactGroup make_exact_group(
        uint32_t agent_count, std::vector<ExactBond> bonds);

    // Set the per-agent goal field = the sum of the given goals' fields (each
    // agent's goals add). Replaces any previously set goal field.
    void set_goals(ExactGroup& g, const std::vector<Goal>& goals);

    // One imaginary-time Trotter step toward the group's entangled ground state:
    // a transverse-field (gamma) relaxation on every agent qubit, then an
    // imaginary-time ZZ on every bond.
    void relax_step(ExactGroup& g, double gamma, double dtau);
    void relax(ExactGroup& g, double gamma, double dtau, uint32_t iterations);

    // Agent i's decision marginal: <sigma_z> in [-1, 1]. Same read surface as
    // the mean-field backend.
    double decision_z(const ExactGroup& g, uint32_t agent);

    // Connected correlation <sz_a sz_b> - <sz_a><sz_b> -- the entanglement
    // witness, nonzero exactly when the two agents are correlated beyond a
    // product state.
    double connected_correlation(const ExactGroup& g, uint32_t a, uint32_t b);
}
