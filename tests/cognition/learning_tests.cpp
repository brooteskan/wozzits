#include <cognition/learning.h>

#include <cognition/exact_group.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using namespace wz::engine::cognition;

namespace
{
    // Probability of one configuration of the learned table. The layer stores
    // unnormalized LOG weights, so this is the same logsumexp the readers do --
    // and it is why these tests port unchanged from the amplitude form: they
    // always reasoned in probabilities, never in amplitudes.
    double prob(const LearnedTable& t, uint64_t basis)
    {
        const double max_w = *std::max_element(t.log_w.begin(), t.log_w.end());
        double total = 0.0;
        for (double w : t.log_w) {
            total += std::exp(w - max_w);
        }
        return total > 0.0 ? std::exp(t.log_w[basis] - max_w) / total : 0.0;
    }
}

// Repeated reward concentrates the memory on the rewarded branch -- a monotonic
// learning curve toward probability 1.
TEST(Learning, RewardConcentratesMemory)
{
    LearnedTable memory = make_learned_table(2);  // four branches, 0.25 each
    double last = prob(memory, 0b11);
    for (int i = 0; i < 40; ++i) {
        reward(memory, /*mask=*/0b11, /*match=*/0b11, /*strength=*/0.5);
        const double p = prob(memory, 0b11);
        EXPECT_GE(p, last - 1e-12);  // monotonic
        last = p;
    }
    EXPECT_GT(prob(memory, 0b11), 0.99);
}

// Switching which branch is rewarded re-concentrates the memory: the agent
// unlearns the old preference and learns the new one.
TEST(Learning, SwitchingRewardRelearns)
{
    LearnedTable memory = make_learned_table(2);
    for (int i = 0; i < 40; ++i) {
        reward(memory, 0b11, 0b11, 0.5);  // learn |11>
    }
    ASSERT_GT(prob(memory, 0b11), 0.99);

    for (int i = 0; i < 80; ++i) {
        reward(memory, 0b11, 0b00, 0.5);  // now reward |00>
    }
    EXPECT_GT(prob(memory, 0b00), 0.95);
    EXPECT_LT(prob(memory, 0b11), 0.05);
}

// A reward on one fact leaves the others exactly where they were. The table is a
// separate structure from the coordination, so nothing a decision does can reach
// it -- which is the real reason a learned bias survives commits, rearms and
// reshapes (it is not, as the old docs implied, anything about coherence).
TEST(Learning, RewardingOneFactLeavesTheOthersAlone)
{
    LearnedTable t = make_learned_table(2);   // bit 0 = decision, bit 1 = memory
    for (int i = 0; i < 20; ++i) {
        reward(t, /*mask=*/0b10, /*match=*/0b10, 0.4);  // bias bit 1 -> 1
    }
    EXPECT_LT(memory_preference(t, 1), -0.5);          // learned the 1 branch
    EXPECT_NEAR(memory_preference(t, 0), 0.0, 1e-9);   // bit 0 untouched
}

// The full loop: a learned memory biases the decision. Feeding the memory's
// preference as a goal field makes the decision commit the way the agent learned
// -- and relearning the memory flips the decision.
TEST(Learning, MemoryBiasesTheDecision)
{
    auto decide_from_memory = [](const LearnedTable& memory) {
        const double pref = memory_preference(memory, 0);  // [-1, 1]
        ExactGroup decision = make_exact_group(1, {});
        set_goals(decision, { Goal{ .agent = 0, .field = pref } });
        relax(decision, 0.1, 0.05, 300);
        return decision_z(decision, 0);
    };

    LearnedTable memory = make_learned_table(1);
    for (int i = 0; i < 20; ++i) {
        reward(memory, /*mask=*/0b1, /*match=*/0b0, 0.4);  // prefer |0> (+z)
    }
    EXPECT_GT(decide_from_memory(memory), 0.9);  // decision commits +z

    for (int i = 0; i < 40; ++i) {
        reward(memory, 0b1, 0b1, 0.4);  // relearn: prefer |1> (-z)
    }
    EXPECT_LT(decide_from_memory(memory), -0.9);  // decision flips
}

// The failure mode the log-weight form deletes by construction. The amplitude
// version multiplied by exp(strength) and renormalized, so a strength past ~709
// overflowed to inf, normalize() turned that into NaN, and the NaN then
// propagated through memory_preference into the goal field and every marginal --
// the agent never committed again. A clamp to +/-50 existed only to avoid it.
// Adding in log space cannot overflow at any plausible magnitude.
TEST(Learning, AbsurdStrengthSaturatesInsteadOfPoisoning)
{
    LearnedTable t = make_learned_table(2);

    reward(t, /*mask=*/0b1, /*match=*/0b1, /*strength=*/1e6);
    // Saturated toward the rewarded branch, and still a FINITE, in-range read.
    const double after = memory_preference(t, 0);
    EXPECT_TRUE(std::isfinite(after));
    EXPECT_NEAR(after, -1.0, 1e-9);          // the 1 branch
    EXPECT_GT(prob(t, 0b01) + prob(t, 0b11), 0.999);

    // And still ALIVE: a larger opposite reward moves it back. A poisoned table
    // would be frozen wherever the guard pinned it.
    reward(t, 0b1, 0b0, 2e6);
    const double back = memory_preference(t, 0);
    EXPECT_TRUE(std::isfinite(back));
    EXPECT_NEAR(back, +1.0, 1e-9);

    // Enormous NEGATIVE strength is equally safe (it used to risk zeroing a
    // branch into a divide-by-zero).
    reward(t, 0b1, 0b0, -1e9);
    EXPECT_TRUE(std::isfinite(memory_preference(t, 0)));
}

// Reward is additive in log space, so the order rewards arrive in cannot matter
// and two half-strength rewards equal one full-strength one. Both properties are
// what "monotonic and saturating" is supposed to mean, and neither was obvious in
// the amplitude form (where each reward also renormalized the whole register).
TEST(Learning, RewardsComposeAdditively)
{
    LearnedTable a = make_learned_table(2);
    reward(a, 0b1, 0b1, 0.3);
    reward(a, 0b10, 0b10, 0.7);

    LearnedTable b = make_learned_table(2);
    reward(b, 0b10, 0b10, 0.7);      // same rewards, opposite order
    reward(b, 0b1, 0b1, 0.3);

    LearnedTable c = make_learned_table(2);
    reward(c, 0b1, 0b1, 0.15);       // one reward split in two
    reward(c, 0b1, 0b1, 0.15);
    reward(c, 0b10, 0b10, 0.7);

    for (uint64_t k = 0; k < 4; ++k) {
        EXPECT_NEAR(prob(a, k), prob(b, k), 1e-12) << "k = " << k;
        EXPECT_NEAR(prob(a, k), prob(c, k), 1e-12) << "k = " << k;
    }
}

// The table is a joint distribution: it can hold a CORRELATION between two facts
// that no independent pair of per-fact weights could. This is the one property
// the layer genuinely needs -- and it is a property of a correlated classical
// table, not of anything quantum.
TEST(Learning, HoldsCorrelationsAnIndependentTableCouldNot)
{
    LearnedTable t = make_learned_table(2);
    for (int i = 0; i < 30; ++i) {
        reward(t, /*mask=*/0b11, /*match=*/0b00, 0.5);   // both 0
        reward(t, 0b11, 0b11, 0.5);                      // or both 1
    }
    // Each fact on its own is undecided...
    EXPECT_NEAR(memory_preference(t, 0), 0.0, 0.05);
    EXPECT_NEAR(memory_preference(t, 1), 0.0, 0.05);
    // ...yet they are perfectly correlated.
    EXPECT_GT(memory_correlation_zz(t, 0, 1), 0.95);
}
