#pragma once

// cognition/coordination.h
//
// One marginal-oriented contract in front of the three coordination backends, so
// the runtime (the quantum_agent decider) depends on the contract, not on a
// particular backend -- and chi is the selector: chi = 1 -> mean-field, finite ->
// the chi-truncated TTN, infinite -> the exact joint state. The caller builds the
// backend it wants (make_exact_group / make_ttn_chain / a mean-field network) and
// assigns it into a Coordination; relax() and decision_z() then dispatch
// uniformly. The read surface is already uniform across the backends -- each has
// relax(x, gamma, dtau, iters) and decision_z(x, agent) -- so this is a thin
// std::visit wrapper; its value is the SEAM (the entangled TTN backend can be
// swapped in without touching the decider).

#include <cognition/exact_group.h>
#include <cognition/graph_tn.h>
#include <cognition/loopy_bp.h>
#include <cognition/ttn.h>

#include <cstdint>
#include <variant>
#include <vector>

namespace wz::engine::cognition
{
    // The backends behind the seam, by what they can represent:
    //   ExactGroup   chi = infinity -- the whole joint state, any topology, cost
    //                exponential in group size.
    //   TtnChain     chi >= 2 on a nearest-neighbour CHAIN -- the fast path when
    //                the topology happens to be a chain.
    //   GraphTn      chi >= 2 on ANY graph -- rings, stars, villages. Slower than
    //                the chain specialization, which is the only reason both exist.
    //   LoopyBpGroup chi = 1 -- any topology, linear, but a product state with no
    //                entanglement across a bond.
    //
    // MeanFieldNetwork is deliberately NOT here. It was in the variant but
    // unbuildable (chi == 1 routes to loopy), so every std::visit carried a branch
    // that could not run; the type still exists for direct use and its own tests.
    using Coordination =
        std::variant<ExactGroup, TtnChain, GraphTn, LoopyBpGroup>;

    // Advance the coordination one relaxation (dispatches to the held backend).
    void relax(
        Coordination& c, double gamma, double dtau, uint32_t iterations);

    // Read an agent's decision marginal <sigma_z> in [-1, 1].
    double decision_z(Coordination& c, uint32_t agent);

    // The most recent relaxation's truncation error for the held backend -- 0 for
    // mean-field (chi=1) and exact (chi=inf, no truncation), the TTN's accumulated
    // last_truncation_error otherwise. Live fidelity telemetry for the chi-capped
    // backend.
    double truncation_error(const Coordination& c);

    // Read EVERY agent's decision marginal in one pass (dispatches to the backend;
    // the TTN backend does a single BP sweep instead of one per agent).
    std::vector<double> decisions(Coordination& c);

    // Collapse agent `agent`'s decision onto `bit` -- project so the OTHER agents
    // are conditioned on the committed outcome (a coupled decision now respects
    // the bond instead of being sampled independently).
    //
    // CONTRACT, uniform across every backend: a collapsed decision is a HELD
    // CONSTRAINT, not a one-shot projection. The backend records the clamp and
    // re-applies it inside every subsequent relaxation step, so the decision stays
    // pinned and its coupled partners deliberate against it for the whole tick.
    // The hold clears only when the coordination is REBUILT (the store's rearm /
    // reshape) -- there is no unclamp.
    //
    // This used to differ per backend, which made the same authored mind behave
    // differently depending on chi: loopy_bp held its clamp, while exact and TTN
    // projected once and let the next relaxation mix it back out (measured: a
    // latched decision drifted -1.0 -> -0.92 in one think). For a squad
    // conditioning on its leader's committed order that is a behavioral
    // difference, not a numerical one.
    void collapse(Coordination& c, uint32_t agent, bool bit);

    // Replace the per-agent longitudinal goal fields live (dispatches to the held
    // backend). Lets a running agent re-bias its decisions -- e.g. from changed
    // world state -- ahead of a re-anneal. Mean-field goals are not wired yet, so
    // that backend is a no-op.
    void set_goals(Coordination& c, const std::vector<Goal>& goals);

    // Measure agent `agent` along axis theta with back-action (dispatches to the
    // held backend). The rotated-basis, non-commuting readout: on an entanglement-
    // capable backend (exact, or TTN with chi capturing the state) it yields
    // non-classical correlations; the product-state (chi = 1) backends report the
    // classical floor. Returns the outcome bit.
    //
    // The measured agent is CLAMPED by the same contract as collapse() -- the
    // rotation leaves it in a definite z state, and that state is then held. Note
    // the partners are conditioned in the COORDINATION immediately, but any cached
    // marginals the caller keeps are stale until it re-reads decisions().
    bool measure_in_basis(
        Coordination& c, uint32_t agent, double theta, qstate::Rng& rng);
}
