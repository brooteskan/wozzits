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
#include <cognition/loopy_bp.h>
#include <cognition/mean_field.h>
#include <cognition/ttn.h>

#include <cstdint>
#include <variant>
#include <vector>

namespace wz::engine::cognition
{
    using Coordination =
        std::variant<MeanFieldNetwork, ExactGroup, TtnChain, LoopyBpGroup>;

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

    // Collapse agent `agent`'s decision onto `bit` -- project + renormalize so the
    // OTHER agents are conditioned on the committed outcome (a coupled decision now
    // respects the bond instead of being sampled independently). Called when a
    // decision latches; re-applied each think while it stays latched (relaxation
    // re-mixes a projected qubit).
    void collapse(Coordination& c, uint32_t agent, bool bit);

    // Replace the per-agent longitudinal goal fields live (dispatches to the held
    // backend). Lets a running agent re-bias its decisions -- e.g. from changed
    // world state -- ahead of a re-anneal. Mean-field goals are not wired yet, so
    // that backend is a no-op.
    void set_goals(Coordination& c, const std::vector<Goal>& goals);
}
