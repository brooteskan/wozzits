#pragma once

// engine/cognition/conditional_policy.h
//
// Genuinely-quantum learning: a CONTEXT-DEPENDENT policy encoded as ENTANGLEMENT
// between a context (memory) qubit and a decision qubit, rather than the
// classical readout of learning.h (read memory -> classical goal field).
//
// reward_pair() reinforces a (context, action) joint branch. Learning the
// "diagonal" -- reward (ctx 0, act 0) and (ctx 1, act 1) -- drives the joint
// state toward (|c0 a0> + |c1 a1>)/sqrt2: an ENTANGLED conditional policy. The
// agent then holds a superposition of context-action pairs: its decision is
// marginally UNDECIDED (<sigma_z> ~ 0) yet CONDITIONALLY definite, and observing
// the context (qstate::measure) collapses the decision to the learned action for
// that context. A separable (product) memory cannot do this -- its decision
// distribution is the same regardless of the context. policy_correlation() is the
// witness: nonzero exactly when the action depends on the context.

#include <engine/qstate/qstate.h>

#include <cstdint>

namespace wz::cognition
{
    // Reinforce the joint branch where context qubit == ctx_value AND decision
    // qubit == dec_value, by e^{strength} (then renormalize). Repeated across
    // (context, action) pairs this learns a conditional policy as entanglement.
    void reward_pair(
        wz::qstate::Register& reg,
        uint32_t ctx_qubit,
        uint8_t ctx_value,
        uint32_t dec_qubit,
        uint8_t dec_value,
        double strength);

    // Connected correlation <sz_ctx sz_dec> - <sz_ctx><sz_dec> between context and
    // decision -- the witness that the learned policy is entangled / context-
    // dependent. Zero for a separable memory.
    double policy_correlation(
        const wz::qstate::Register& reg, uint32_t ctx_qubit, uint32_t dec_qubit);
}
