#pragma once

// cognition/conditional_policy.h
//
// CONTEXT-DEPENDENT policy: what to do depends on what is true. reward_pair()
// reinforces a joint (context, action) branch of the learned table, so learning
// the "diagonal" -- reward (ctx 0, act 0) and (ctx 1, act 1) -- produces a table
// in which the action is UNDECIDED marginally (<sigma_z> ~ 0) yet DEFINITE given
// the context. conditional_preference() reads that back without disturbing it,
// so it can be folded into a goal field every frame.
//
// What this is and is not: the table holds a CORRELATED joint distribution over
// (context, action). An independent/product table cannot represent
// context-dependence -- that part is real and is what these functions buy. But a
// classical correlated table reproduces every read this API offers, exactly;
// there is nothing here a joint probability table cannot do. policy_correlation
// is a correlation measure, not an entanglement witness.
//
// (This layer used to describe itself as "genuinely-quantum learning" and a
// policy "encoded as ENTANGLEMENT", with policy_correlation as the witness. The
// register it ran on was only ever acted on by diagonal positive-real operations
// and was never measured, so it could not access non-classical structure even in
// principle. See learning.h.)

#include <cognition/learning.h>

#include <cstdint>

namespace wz::engine::cognition
{
    // Reinforce the joint branch where context bit == ctx_value AND decision bit
    // == dec_value. Repeated across (context, action) pairs this learns a
    // conditional policy as a correlation between the two.
    void reward_pair(
        LearnedTable& memory,
        uint32_t ctx_qubit,
        uint8_t ctx_value,
        uint32_t dec_qubit,
        uint8_t dec_value,
        double strength);

    // Connected correlation <sz_ctx sz_dec> - <sz_ctx><sz_dec> between context and
    // decision: nonzero exactly when the learned action depends on the context.
    // Zero for an independent (product) table.
    double policy_correlation(
        const LearnedTable& memory, uint32_t ctx_qubit, uint32_t dec_qubit);

    // Conditional read: <sigma_z> of the DECISION bit GIVEN context == ctx_value,
    // in [-1, 1] (+1 => the learned action for this context is 0). Fold it into
    // the decision's goal field each frame -- it reads the table without changing
    // it, so learning continues. Derived from the one- and two-body moments alone:
    // <sz_dec | ctx=v> = (z_dec + s*zz) / (1 + s*z_ctx) with s = +1 for ctx_value
    // 0, -1 for 1 (denominator = 2*P(ctx == ctx_value)). Returns 0 when that
    // context has ~zero probability.
    //
    // Cheap to call every frame: the moments come from the table's cached
    // marginals, so this is O(1) between rewards rather than three full 2^n passes
    // per call.
    double conditional_preference(
        const LearnedTable& memory,
        uint32_t ctx_qubit,
        uint8_t ctx_value,
        uint32_t dec_qubit);
}
