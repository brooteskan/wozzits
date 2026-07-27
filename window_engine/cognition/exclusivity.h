#pragma once

// cognition/exclusivity.h
//
// Soft one-hot exclusivity over an agent's dispositions. Multi-qubit agents can
// hold several dispositions at once (they need not be exclusive), but when a
// disposition set IS a mutually-exclusive choice (flee | fight | hide -- pick
// one), add_one_hot biases the agent toward EXACTLY ONE active disposition.
//
// It encodes the penalty A * (N_active - 1)^2, where N_active = sum_i (1-sz_i)/2,
// which expands to:
//   * antiferromagnetic pairwise bonds (coupling j = -A/2) among all the agent's
//     disposition qubits -- so two active at once costs energy, and
//   * an "inactive" longitudinal bias h = A*(k-2)/2 on each disposition (k = the
//     disposition count) -- so the penalty is minimized at exactly one active.
// The binary case (k = 2) reduces to a single antiferro bond (h = 0). A
// tiebreaker goal selects which disposition wins; without one the choice stays a
// symmetric superposition of the k one-hot options (each active with weight 1/k).

#include <cognition/agent_layout.h>
#include <cognition/exact_group.h>

#include <cstdint>
#include <vector>

namespace wz::engine::cognition
{
    // Expand `agent`'s soft one-hot into SPEC-level terms: antiferro bonds among
    // its dispositions appended to `bonds`, and the inactive bias appended to
    // `goals`. This is the form the AgentCognitionStore uses, because it feeds the
    // terms through canonicalize_bonds along with the authored ones -- so the
    // one-hot works on every backend, not just the exact one, and its bonds get
    // the same girth/self-bond hygiene as everything else.
    //
    // Terms naming a qubit at or past `qubit_count` are DROPPED, as are unknown
    // agents. Appends nothing when `strength` is 0.
    void add_one_hot(
        std::vector<ExactBond>& bonds,
        std::vector<Goal>& goals,
        const AgentLayout& layout,
        uint32_t agent,
        double strength,
        uint32_t qubit_count);

    // Append the soft one-hot terms for `agent`'s dispositions to the group.
    // Additive: call after make_exact_group / set_goals (it pushes bonds and adds
    // to goal_field). `strength` is the penalty weight A.
    //
    // Terms naming a qubit the group does not have are DROPPED (an unknown
    // agent, or a layout wider than the group). These bonds land after
    // make_exact_group's endpoint guard has already run, so this is the only
    // thing standing between a mismatched layout and an unchecked ZZ on a
    // nonexistent qubit.
    void add_one_hot(
        ExactGroup& g,
        const AgentLayout& layout,
        uint32_t agent,
        double strength);
}
