#include <cognition/commit.h>

#include <cognition/qstate/qstate.h>

#include <gtest/gtest.h>

using namespace wz::engine::cognition;
using wz::engine::cognition::qstate::Register;
using wz::engine::cognition::qstate::Rng;

namespace
{
    // A register relaxed to a confident |0> (P0 -> 1).
    Register decided_zero()
    {
        Register reg = wz::engine::cognition::qstate::uniform(1);
        for (int i = 0; i < 200; ++i) {
            wz::engine::cognition::qstate::apply_imag_time_field(reg, 0, /*gamma=*/0.0, /*h=*/0.5, 0.05);
        }
        return reg;
    }
}

TEST(Commit, ConfidentReflectsLeadingProbability)
{
    Register undecided = wz::engine::cognition::qstate::uniform(1);  // P0 = P1 = 0.5
    EXPECT_FALSE(confident(undecided, 0, 0.8));

    Register sure = decided_zero();               // P0 ~ 1
    EXPECT_TRUE(confident(sure, 0, 0.8));
}

// A confident agent commits on demand: the disposition collapses to its leading
// outcome.
TEST(Commit, CommitsWhenConfident)
{
    Register reg = decided_zero();
    Rng rng{ 1u };
    const auto decision = try_commit(reg, 0, CommitPolicy{ .confidence = 0.8 },
        /*dt=*/0.1, rng);
    ASSERT_TRUE(decision.has_value());
    EXPECT_FALSE(*decision);                       // decided 0
    EXPECT_NEAR(wz::engine::cognition::qstate::marginal(reg, 0), 0.0, 1e-9);  // collapsed
}

// An undecided agent with no environmental pressure keeps deliberating -- it
// never commits and stays in superposition.
TEST(Commit, DeliberatesWhenUndecidedAndNoDecoherence)
{
    Register reg = wz::engine::cognition::qstate::uniform(1);
    Rng rng{ 2u };
    const CommitPolicy policy{ .confidence = 0.8, .decoherence_rate = 0.0 };
    for (int i = 0; i < 200; ++i) {
        EXPECT_FALSE(try_commit(reg, 0, policy, 0.1, rng).has_value());
    }
    EXPECT_NEAR(wz::engine::cognition::qstate::marginal(reg, 0), 0.5, 1e-9);  // still superposed
}

// Decoherence (environmental pressure) forces a snap decision on an undecided
// agent, and the outcome follows the Born rule (50/50 for an even superposition).
TEST(Commit, DecoherenceForcesCommitmentWithBornOutcome)
{
    // confidence 1.1 so the threshold never fires; decoherence p = 1 per step.
    const CommitPolicy policy{ .confidence = 1.1, .decoherence_rate = 100.0 };
    Rng rng{ 0xBEEFu };
    const int trials = 4000;
    int committed = 0;
    int ones = 0;
    for (int t = 0; t < trials; ++t) {
        Register reg = wz::engine::cognition::qstate::uniform(1);
        const auto d = try_commit(reg, 0, policy, /*dt=*/1.0, rng);
        if (d.has_value()) {
            ++committed;
            if (*d) {
                ++ones;
            }
        }
    }
    EXPECT_EQ(committed, trials);  // pressure forces every one to commit
    EXPECT_NEAR(static_cast<double>(ones) / trials, 0.5, 0.03);  // Born rule
}

// The decoherence RATE controls how readily an agent snaps: a low rate mostly
// keeps deliberating per step, a high rate almost always commits.
TEST(Commit, DecoherenceRateControlsCommitLatency)
{
    Rng rng{ 7u };
    const int trials = 4000;

    int low = 0;
    const CommitPolicy slow{ .confidence = 1.1, .decoherence_rate = 0.5 };
    for (int t = 0; t < trials; ++t) {
        Register reg = wz::engine::cognition::qstate::uniform(1);
        if (try_commit(reg, 0, slow, /*dt=*/0.1, rng).has_value()) {  // p = 0.05
            ++low;
        }
    }

    int high = 0;
    const CommitPolicy fast{ .confidence = 1.1, .decoherence_rate = 20.0 };
    for (int t = 0; t < trials; ++t) {
        Register reg = wz::engine::cognition::qstate::uniform(1);
        if (try_commit(reg, 0, fast, /*dt=*/0.1, rng).has_value()) {  // p = 1
            ++high;
        }
    }

    EXPECT_LT(static_cast<double>(low) / trials, 0.1);   // mostly deliberates
    EXPECT_GT(static_cast<double>(high) / trials, 0.99);  // mostly snaps
}
