#pragma once

// engine/cognition/learning.h
//
// The memory + learning loop. An agent's register splits into a DECISION
// subregister (its dispositions, which collapse when it commits) and a MEMORY
// subregister, which is NEVER measured -- so it stays coherent and accumulates
// across episodes. Learning rides the memory:
//
//   * reward() concentrates the memory on the branches that worked -- dissipative
//     amplitude amplification (boost the matched amplitudes, renormalize). Unlike
//     a unitary Grover step it is MONOTONIC and saturating, so repeated rewards
//     converge a branch toward probability 1 (a learning curve), and switching
//     the rewarded branch re-concentrates the memory (relearning).
//   * Because the memory is unmeasured, its learned bias survives every decision
//     collapse (partial measurement of the decision subregister leaves a product
//     memory untouched).
//   * memory_preference() reads what the memory has learned (a memory qubit's
//     <sigma_z>); feeding that as a goal field biases the decision -- so the agent
//     leans toward what paid off (see learning_tests for the full loop).

#include <engine/qstate/qstate.h>

#include <cstdint>

namespace wz::cognition
{
    // Reinforce a memory branch: amplify the amplitude of basis states whose
    // bits, masked by `mask`, equal `match`, by e^{strength}, then renormalize.
    // strength > 0 rewards the branch (raises its probability); strength < 0
    // punishes it. The increment is monotonic and saturating.
    void reward(
        wz::qstate::Register& memory,
        uint64_t mask,
        uint64_t match,
        double strength);

    // What the memory has learned about a disposition: <sigma_z> of memory qubit
    // q, in [-1, 1]. Feed it (scaled) as a goal field to bias the decision.
    double memory_preference(const wz::qstate::Register& memory, uint32_t q);
}
