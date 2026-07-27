#pragma once

// cognition/learning.h
//
// ## What this layer is
//
// A CLASSICAL multiplicative-weights table over a joint distribution. Not
// approximately -- exactly. Concretely: `LearnedTable` holds one log-weight per
// configuration of `bits` binary facts, reward() adds to the matched subset, and
// every read is an expectation over p(k) proportional to exp(log_w[k]). That is
// textbook exponentiated-gradient reweighting.
//
// This is deliberate, not a fallback. It is what gameplay actually consumes
// (every consumer wants P(action | context) as a scalar to fold into a goal
// field); it is deterministic and inspectable, so a designer can dump, diff and
// tune it; it is sign-problem-free, which is exactly why it is cheap; and its
// behaviour is genuinely good -- monotonic, saturating, relearns when the reward
// switches, survives decision collapse, and supports contextual policies.
//
// It is held OUTSIDE the coordination and never measured, which is why a learned
// bias accumulates across commits, rearms and reshapes. That survival is because
// it is a SEPARATE TABLE, not because of anything about coherence.
//
// HISTORY, because the shape still shows: this was a qstate::Register whose
// amplitudes were multiplied by e^strength and renormalized. Every operation on
// it was diagonal and positive-real, so for a non-negative state each read was
// identically a classical expectation over |psi|^2 -- the register could not
// represent anything a weight table could not, and it was never measured, so its
// "entanglement" was unobservable in principle rather than merely unobserved. The
// log-weight form is the same math without the cost: no exp, no renormalize pass,
// half the footprint, no complex arithmetic, and no overflow cliff (e^709 -> inf
// -> normalize -> NaN would permanently brick an agent, which is why the old code
// had to clamp strength to +/-50; arbitrarily large strength now just saturates
// P -> 1 smoothly, which was always the documented intent).
//
// The genuinely non-classical primitive in this library is measure_in_basis --
// rotated-basis measurement with back-action. It lives on the COORDINATION, not
// here, and this layer makes no claim on it.

#include <cstdint>
#include <vector>

namespace wz::engine::cognition
{
    // Cap on a learned table's bit count. 2^n doubles, so 16 bits is a 512 KB
    // table -- already far past anything the design needs (real memories in the
    // project are 2-6 bits). The old cap was borrowed from the exact backend's
    // 24-qubit memory guard, i.e. a 256 MB "memory" per agent.
    inline constexpr uint32_t kMaxMemoryBits = 16;

    // A correlated joint distribution over `bits` binary facts, stored as LOG
    // weights. Unnormalized by construction -- every read normalizes, so weights
    // may drift anywhere without overflowing.
    struct LearnedTable
    {
        uint32_t bits = 0;
        std::vector<double> log_w;   // size 1 << bits; starts all zero (uniform)

        // Cached marginals, filled lazily by the readers, so a caller folding
        // conditional_preference into a goal field EVERY FRAME pays for the table
        // scan once per reward rather than three times per read.
        //
        // The one-body values come from a single pass over the table. Two-body
        // values are computed PER PAIR on demand rather than all at once: filling
        // the whole bits x bits matrix eagerly is O(2^n * n^2), which at the
        // 16-bit cap measures 21 ms -- worse than the three targeted passes it
        // replaced. Real callers ask about one or two pairs.
        mutable std::vector<double> z_cache;      // <sigma_z> per bit
        mutable std::vector<double> zz_cache;     // <sigma_z sigma_z>, bits x bits
        mutable std::vector<uint8_t> zz_valid;    // which pairs are filled
        mutable bool z_valid = false;
    };

    // A fresh table over `bits` facts: all weights zero, i.e. the uniform
    // distribution. Empty (bits == 0) is a valid "no memory" table.
    LearnedTable make_learned_table(uint32_t bits);

    // Reinforce a branch: add to the log-weight of every configuration whose bits,
    // masked by `mask`, equal `match`. strength > 0 rewards the branch (raises its
    // probability), < 0 punishes it. Monotonic and saturating in probability:
    // repeated rewards converge the branch toward P = 1 and switching the rewarded
    // branch relearns.
    //
    // The increment is 2 * strength, which is what makes this bit-for-bit the old
    // amplitude form: that multiplied amplitudes by e^strength, and probability is
    // amplitude squared.
    void reward(
        LearnedTable& memory, uint64_t mask, uint64_t match, double strength);

    // What the table has learned about fact `q`: <sigma_z> in [-1, 1], where +1
    // means the bit is 0 and -1 means it is 1. Feed it (scaled) as a goal field to
    // bias a decision. 0 for an out-of-range bit.
    double memory_preference(const LearnedTable& memory, uint32_t q);

    // <sigma_z sigma_z> between two facts, in [-1, 1]: +1 when they always agree,
    // -1 when they always differ. The raw two-body moment behind
    // policy_correlation.
    double memory_correlation_zz(
        const LearnedTable& memory, uint32_t a, uint32_t b);
}
