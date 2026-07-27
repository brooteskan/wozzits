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
        // Held collapses: -1 free, 0 pinned to |0>, 1 pinned to |1>. collapse()
        // records here and relax_step() re-projects every entry each step, so a
        // committed decision stays a CONSTRAINT the still-deliberating agents
        // relax against, rather than a one-shot projection that the very next
        // relaxation mixes back out. Mirrors LoopyBpGroup::clamped, which has
        // always worked this way. Cleared only by rebuilding the group.
        std::vector<int8_t> clamp;
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
    // imaginary-time ZZ on every bond, then a re-projection of every held clamp
    // (see ExactGroup::clamp) so committed decisions stay pinned across the step.
    void relax_step(ExactGroup& g, double gamma, double dtau);
    void relax(ExactGroup& g, double gamma, double dtau, uint32_t iterations);

    // Agent i's decision marginal: <sigma_z> in [-1, 1]. Same read surface as
    // the mean-field backend.
    double decision_z(const ExactGroup& g, uint32_t agent);

    // Every agent's decision marginal in one pass (cheap here; the seam mirror of
    // the TTN bulk read that avoids re-running BP per agent).
    std::vector<double> decisions(const ExactGroup& g);

    // Collapse agent `agent`'s qubit onto `bit` (project + renormalize the joint
    // register) and HOLD it there: the clamp is recorded and re-applied by every
    // subsequent relax_step, so the other agents keep deliberating conditioned on
    // the committed outcome instead of watching it drift back off the latch.
    // Clears only on a rebuild (the store's rearm / reshape).
    void collapse(ExactGroup& g, uint32_t agent, bool bit);

    // Connected correlation <sz_a sz_b> - <sz_a><sz_b> -- the entanglement
    // witness, nonzero exactly when the two agents are correlated beyond a
    // product state.
    double connected_correlation(const ExactGroup& g, uint32_t a, uint32_t b);

    // Measure agent `agent` along axis theta (x-z plane) on the JOINT register,
    // with back-action: the projection conditions the coupled agents through the
    // shared wavefunction, so on an entangled group (chi = 0, full 2^N) this is
    // the non-classical, Bell-capable readout. Returns the outcome bit (see
    // qstate::measure_in_basis).
    bool measure_in_basis(
        ExactGroup& g, uint32_t agent, double theta,
        wz::engine::cognition::qstate::Rng& rng);
}
