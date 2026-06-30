#pragma once

// engine/cognition/exact_group.h
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

#include <engine/qstate/qstate.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace wz::cognition
{
    struct ExactBond
    {
        uint32_t a = 0;
        uint32_t b = 0;
        double j = 0.0;  // > 0 ferromagnetic (agree), < 0 anti
    };

    struct ExactGroup
    {
        wz::qstate::Register joint;  // over `agent_count` qubits
        std::vector<ExactBond> bonds;
    };

    // Joint register starts in equal superposition over all agents.
    ExactGroup make_exact_group(
        uint32_t agent_count, std::vector<ExactBond> bonds);

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
