#include <cognition/conditional_policy.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using namespace wz::engine::cognition;

namespace
{
    // Bit 0 = context, bit 1 = decision.
    constexpr uint32_t kCtx = 0;
    constexpr uint32_t kDec = 1;

    // Learn the "diagonal" policy: in context 0 do action 0, in context 1 do
    // action 1 -> a table whose mass sits on {00, 11}, i.e. the action is
    // undecided marginally but determined by the context.
    LearnedTable learn_diagonal()
    {
        LearnedTable t = make_learned_table(2);
        for (int i = 0; i < 40; ++i) {
            reward_pair(t, kCtx, 0, kDec, 0, 0.5);
            reward_pair(t, kCtx, 1, kDec, 1, 0.5);
        }
        return t;
    }

    // Draw a configuration from the table by its own probabilities.
    uint64_t sample(const LearnedTable& t, double u)
    {
        const double max_w = *std::max_element(t.log_w.begin(), t.log_w.end());
        double total = 0.0;
        for (double w : t.log_w) {
            total += std::exp(w - max_w);
        }
        double acc = 0.0;
        for (std::size_t k = 0; k < t.log_w.size(); ++k) {
            acc += std::exp(t.log_w[k] - max_w) / total;
            if (u <= acc) {
                return k;
            }
        }
        return t.log_w.empty() ? 0u : t.log_w.size() - 1u;
    }
}

// The policy correlation witnesses whether the action depends on the context:
// a diagonal policy is positively correlated, an anti-diagonal one negatively.
TEST(ConditionalPolicy, CorrelationWitnessesContextDependence)
{
    LearnedTable diagonal = learn_diagonal();
    EXPECT_GT(policy_correlation(diagonal, kCtx, kDec), 0.95);

    LearnedTable anti = make_learned_table(2);
    for (int i = 0; i < 40; ++i) {
        reward_pair(anti, kCtx, 0, kDec, 1, 0.5);  // context 0 -> action 1
        reward_pair(anti, kCtx, 1, kDec, 0, 0.5);  // context 1 -> action 0
    }
    EXPECT_LT(policy_correlation(anti, kCtx, kDec), -0.95);
}

// The signature of a conditional policy: after learning, the action is
// marginally UNDECIDED yet fully CORRELATED with the context -- the agent holds a
// definite conditional plan, not a definite action. An independent (product)
// table cannot represent this; a correlated classical one can, which is exactly
// what this is.
TEST(ConditionalPolicy, DecisionIsUndecidedButPolicyIsDefinite)
{
    LearnedTable t = learn_diagonal();
    EXPECT_NEAR(memory_preference(t, kDec), 0.0, 0.05);   // undecided
    EXPECT_GT(policy_correlation(t, kCtx, kDec), 0.95);   // but context-dependent
}

// conditional_preference reads the learned action for a given context without
// disturbing the table: for the diagonal policy, context 0 leans to action 0 (+1)
// and context 1 to action 1 (-1), while the UNconditioned action stays ~0.
TEST(ConditionalPolicy, ConditionalPreferenceReadsTheContextAction)
{
    LearnedTable t = learn_diagonal();
    // Marginally undecided...
    EXPECT_NEAR(memory_preference(t, kDec), 0.0, 0.05);
    // ...but conditionally definite, oppositely per context.
    EXPECT_GT(conditional_preference(t, kCtx, 0, kDec), 0.9);   // ctx 0 -> action 0
    EXPECT_LT(conditional_preference(t, kCtx, 1, kDec), -0.9);  // ctx 1 -> action 1

    // The read leaves the table alone: the policy is still there afterwards.
    EXPECT_GT(policy_correlation(t, kCtx, kDec), 0.95);
}

// A fresh (uniform) memory has no learned action in any context: the conditional
// read is ~0 both ways.
TEST(ConditionalPolicy, ConditionalPreferenceIsFlatBeforeLearning)
{
    LearnedTable t = make_learned_table(2);
    EXPECT_NEAR(conditional_preference(t, kCtx, 0, kDec), 0.0, 1e-6);
    EXPECT_NEAR(conditional_preference(t, kCtx, 1, kDec), 0.0, 1e-6);
}

// Drawing from the learned table always yields an action that matches its
// context -- the joint distribution has no mass anywhere else. This is what
// "conditioning" means here: a correlated table, sampled.
TEST(ConditionalPolicy, SamplingAlwaysPairsTheActionWithItsContext)
{
    const LearnedTable t = learn_diagonal();
    const int trials = 2000;
    for (int i = 0; i < trials; ++i) {
        const double u = (static_cast<double>(i) + 0.5) / trials;
        const uint64_t k = sample(t, u);
        const bool context = ((k >> kCtx) & 1u) != 0;
        const bool action = ((k >> kDec) & 1u) != 0;
        EXPECT_EQ(action, context) << "sample " << i;
    }
}
