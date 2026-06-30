#pragma once

// engine/cognition/commit.h
//
// When does a deliberating agent COLLAPSE a disposition into a committed action?
// The policy is the hybrid we settled on: a confidence threshold OR decoherence
// from environmental pressure (the on-demand trigger is just calling try_commit
// when the actuator needs an output).
//
//   * Threshold -- the agent "makes up its mind" once its leading outcome's
//     probability crosses `confidence`.
//   * Decoherence -- stress / observation / damage forces a snap decision even
//     while undecided. We model it as STOCHASTIC MEASUREMENT (a quantum
//     trajectory): with probability decoherence_rate * dt the disposition is
//     measured. That keeps every agent a pure state (no density matrix) while
//     reproducing dephasing in aggregate -- high pressure => frequent collapse
//     => no sustained superposition => snap decisions; low pressure => long
//     coherent deliberation.
//
// Measurement is PARTIAL (qstate::measure): only the named decision qubit
// collapses; the rest of the agent's register (other dispositions, memory)
// stays coherent.

#include <engine/qstate/qstate.h>

#include <cstdint>
#include <optional>

namespace wz::cognition
{
    struct CommitPolicy
    {
        double confidence = 0.8;        // commit when max(P0, P1) >= this
        double decoherence_rate = 0.0;  // per unit time; environmental pressure
    };

    // Is the agent confident enough to commit qubit q? (leading outcome
    // probability >= confidence).
    bool confident(
        const wz::qstate::Register& reg, uint32_t q, double confidence);

    // Try to commit decision qubit q over a time step dt:
    //   - if confident -> measure (collapse) and return the decided bit;
    //   - else with probability decoherence_rate*dt -> measure and return it;
    //   - else nullopt (the agent keeps deliberating, stays in superposition).
    std::optional<bool> try_commit(
        wz::qstate::Register& reg,
        uint32_t q,
        const CommitPolicy& policy,
        double dt,
        wz::qstate::Rng& rng);

    // MARGINAL-level commit -- the same policy as try_commit, but driven by an
    // agent's decision marginal z = <sigma_z> in [-1, 1] rather than a qstate
    // register. The COORDINATION backends expose only the marginal (decision_z),
    // not a single shared register the way a lone agent does (the TTN holds
    // tensors; the exact backend a joint register over the whole group), so a
    // coordinated agent commits on its marginal. P(|1>) = (1 - z)/2.
    bool confident_marginal(double z, double confidence);

    //   - confident -> commit to the LEADING disposition (deterministic);
    //   - else decoherence (prob decoherence_rate*dt) -> Born-sample the bit from z;
    //   - else nullopt (keep deliberating).
    // The decided bit is true for |1> (z < 0), false for |0> (z > 0).
    std::optional<bool> try_commit_marginal(
        double z,
        const CommitPolicy& policy,
        double dt,
        wz::qstate::Rng& rng);
}
