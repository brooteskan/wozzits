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
#include <cognition/mean_field.h>
#include <cognition/ttn.h>

#include <cstdint>
#include <variant>

namespace wz::engine::cognition
{
    using Coordination = std::variant<MeanFieldNetwork, ExactGroup, TtnChain>;

    // Advance the coordination one relaxation (dispatches to the held backend).
    void relax(
        Coordination& c, double gamma, double dtau, uint32_t iterations);

    // Read an agent's decision marginal <sigma_z> in [-1, 1].
    double decision_z(Coordination& c, uint32_t agent);
}
