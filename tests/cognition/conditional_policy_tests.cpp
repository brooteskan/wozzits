#include <engine/cognition/conditional_policy.h>

#include <engine/qstate/qstate.h>

#include <gtest/gtest.h>

using namespace wz::cognition;
using wz::qstate::Register;

namespace
{
    // Qubit 0 = context (memory), qubit 1 = decision.
    constexpr uint32_t kCtx = 0;
    constexpr uint32_t kDec = 1;

    // Learn the "diagonal" policy: in context 0 do action 0, in context 1 do
    // action 1 -> the entangled state (|c0 a0> + |c1 a1>)/sqrt2.
    Register learn_diagonal()
    {
        Register reg = wz::qstate::uniform(2);
        for (int i = 0; i < 40; ++i) {
            reward_pair(reg, kCtx, 0, kDec, 0, 0.5);
            reward_pair(reg, kCtx, 1, kDec, 1, 0.5);
        }
        return reg;
    }
}

// The policy correlation witnesses whether the action depends on the context:
// a diagonal policy is positively correlated, an anti-diagonal one negatively.
TEST(ConditionalPolicy, CorrelationWitnessesContextDependence)
{
    Register diagonal = learn_diagonal();
    EXPECT_GT(policy_correlation(diagonal, kCtx, kDec), 0.95);

    Register anti = wz::qstate::uniform(2);
    for (int i = 0; i < 40; ++i) {
        reward_pair(anti, kCtx, 0, kDec, 1, 0.5);  // context 0 -> action 1
        reward_pair(anti, kCtx, 1, kDec, 0, 0.5);  // context 1 -> action 0
    }
    EXPECT_LT(policy_correlation(anti, kCtx, kDec), -0.95);
}

// The genuinely-quantum signature: after learning, the decision is marginally
// UNDECIDED (it has not committed) yet the policy is fully entangled -- the agent
// holds a definite CONDITIONAL plan, not a definite action.
TEST(ConditionalPolicy, DecisionIsUndecidedButPolicyIsDefinite)
{
    Register reg = learn_diagonal();
    EXPECT_NEAR(wz::qstate::expectation_z(reg, kDec), 0.0, 0.05);  // undecided
    EXPECT_GT(policy_correlation(reg, kCtx, kDec), 0.95);          // but entangled
}

// Observing the context collapses the decision to the learned action for that
// context -- conditioning through entanglement, not a classical readout.
TEST(ConditionalPolicy, ObservingContextSelectsTheLearnedAction)
{
    wz::qstate::Rng rng{ 0x515Eu };
    const int trials = 2000;
    for (int t = 0; t < trials; ++t) {
        Register reg = learn_diagonal();
        const bool context = wz::qstate::measure(reg, kCtx, rng);
        const bool action = wz::qstate::measure(reg, kDec, rng);
        EXPECT_EQ(action, context);  // diagonal policy: action matches context
    }
}
