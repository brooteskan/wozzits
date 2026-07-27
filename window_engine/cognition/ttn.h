#pragma once

// cognition/ttn.h
//
// The chi-truncated tree tensor network coordination backend -- the scalable
// middle of the coordination dial, between mean-field (chi = 1, no entanglement)
// and the exact joint-state backend (chi = infinity, exponential). A chain of
// single-qubit agents is held as an MPS and evolved by imaginary-time Trotter
// gates -- a transverse field (+ optional goal) per agent, a nearest-neighbour
// coupling per bond -- with each coupling's entanglement capped at chi via the
// SVD truncation (apply_two_site_gate). Marginals come from exact tree BP
// (tree_bp_sigma_z). Cost is LINEAR in chain length and polynomial in chi, so it
// delivers genuine (bounded) entanglement for large groups.
//
// At chi large enough for no truncation it reproduces the exact backend exactly;
// at chi = 1 it degrades toward a product state. The read surface (decisions /
// decision_z) matches the other backends, so chi is the selector behind the
// coordination contract. First cut: a CHAIN with nearest-neighbour couplings
// (the MPS structure); branching trees and long-range couplings are follow-ups.

#include <cognition/qstate/qstate.h>
#include <cognition/tree_bp.h>

#include <cstdint>
#include <vector>

namespace wz::engine::cognition
{
    struct TtnChain
    {
        TreeBpNetwork mps;                // chain of single-qubit agents
        std::vector<double> coupling;     // size n-1: j between agent i and i+1
        std::vector<double> goal_field;   // size n: per-agent longitudinal goal
        uint32_t chi = 4;                 // bond-dimension cap
        // Held collapses: -1 free, 0 pinned to |0>, 1 pinned to |1>. collapse()
        // records here and relax_step() re-applies every entry each step, so a
        // committed decision stays a CONSTRAINT the still-deliberating agents
        // relax against instead of being mixed back out by the next step. Same
        // convention as ExactGroup::clamp and LoopyBpGroup::clamped. Cleared only
        // by rebuilding the chain.
        std::vector<int8_t> clamp;
        // Sum of the per-bond relative truncation errors from the most recent
        // relax_step (0 for a full-chi/untruncated sweep). Telemetry only; not
        // part of the state.
        double last_truncation_error = 0;
        // Working buffers for the per-bond two-site gates, carried on the chain so
        // a relax sweep allocates nothing. Not state: copying or omitting it
        // changes only speed, never results.
        TwoSiteScratch scratch;
    };

    // A chain of `n` single-qubit agents in the product |+> state with the given
    // nearest-neighbour couplings (size n-1) and bond cap chi; goals start zero.
    TtnChain make_ttn_chain(
        uint32_t n, std::vector<double> coupling, uint32_t chi);

    // One imaginary-time Trotter step toward the group's ground state: a single-
    // site transverse field (gamma) + goal on each agent, then a truncated
    // imaginary-time ZZ on each bond.
    void relax_step(TtnChain& g, double gamma, double dtau);
    void relax(TtnChain& g, double gamma, double dtau, uint32_t iterations);

    // Each agent's decision marginal <sigma_z> via exact tree BP. Same read
    // surface as the mean-field and exact backends.
    std::vector<double> decisions(TtnChain& g);
    double decision_z(TtnChain& g, uint32_t agent);

    // Collapse agent `agent`'s site onto `bit`: zero the opposite physical
    // component of the MPS site. No renormalize -- tree_bp_sigma_z normalizes by
    // trace -- so subsequent reads reflect the committed value and the rest of the
    // chain is conditioned through the bonds. Also HELD: the clamp is recorded and
    // re-applied by every later relax_step, so the committed decision stays a
    // constraint rather than decaying. Clears only on a rebuild.
    void collapse(TtnChain& g, uint32_t agent, bool bit);

    // Measure agent `agent` along axis theta (x-z plane), with back-action, up to
    // the bond dimension chi. Applies the R_y single-site rotation, Born-samples
    // the site from its CONDITIONED marginal, then collapses it -- the projection
    // conditions the rest of the chain through the bonds. chi >= 2 captures a
    // rank-<= chi entangled state (chi = 2 is EXACT for a pair / any GHZ cut), so
    // this is a genuine non-classical readout. Returns the outcome bit.
    bool measure_in_basis(
        TtnChain& g, uint32_t agent, double theta,
        wz::engine::cognition::qstate::Rng& rng);
}
