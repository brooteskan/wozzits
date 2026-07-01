#include <cognition/exact_group.h>

#include <gtest/gtest.h>

#include <cmath>

using namespace wz::engine::cognition;

// A goal field commits an otherwise-undecided agent: pursuit is just the
// relaxation toward the goal-biased ground state. Positive field -> +z.
TEST(Goals, GoalCommitsADecision)
{
    ExactGroup g = make_exact_group(1, {});
    set_goals(g, { Goal{ .agent = 0, .field = 0.5 } });
    relax(g, /*gamma=*/0.1, /*dtau=*/0.05, /*iterations=*/300);
    EXPECT_GT(decision_z(g, 0), 0.95);

    ExactGroup g2 = make_exact_group(1, {});
    set_goals(g2, { Goal{ .agent = 0, .field = -0.5 } });
    relax(g2, 0.1, 0.05, 300);
    EXPECT_LT(decision_z(g2, 0), -0.95);
}

// Opposing goals on the same agent sum to zero -> the agent stays undecided.
TEST(Goals, OpposingGoalsCancel)
{
    ExactGroup g = make_exact_group(1, {});
    set_goals(g, { Goal{ 0, 0.5 }, Goal{ 0, -0.5 } });
    relax(g, 0.1, 0.05, 300);
    EXPECT_NEAR(decision_z(g, 0), 0.0, 0.05);
}

// A goal propagates through coordination: only agent 0 has a +z goal, but the
// ferromagnetic bond drags its partner along, so both commit +z.
TEST(Goals, GoalPropagatesThroughCoupling)
{
    ExactGroup g = make_exact_group(2, { ExactBond{ 0, 1, 1.0 } });
    set_goals(g, { Goal{ .agent = 0, .field = 0.5 } });
    relax(g, 0.1, 0.05, 400);
    EXPECT_GT(decision_z(g, 0), 0.9);
    EXPECT_GT(decision_z(g, 1), 0.9);  // pulled along by the coupling
}

// FRUSTRATION: a triangle of three antiferromagnetic bonds cannot have all
// three pairs disagree at once. The relaxed state is the classic frustrated
// compromise -- every pair only partially anti-correlated (~ -1/3), nowhere near
// the -1 an unfrustrated antiferromagnetic pair reaches. Conflicting constraints
// produce wavering, not a clean decision.
TEST(Goals, FrustratedTriangleCannotSatisfyAllConstraints)
{
    ExactGroup g = make_exact_group(
        3, { ExactBond{ 0, 1, -1.0 }, ExactBond{ 1, 2, -1.0 },
            ExactBond{ 0, 2, -1.0 } });
    relax(g, /*gamma=*/0.1, /*dtau=*/0.05, /*iterations=*/400);

    const double c01 = connected_correlation(g, 0, 1);
    const double c12 = connected_correlation(g, 1, 2);
    const double c02 = connected_correlation(g, 0, 2);

    // Each pair is anti-correlated but only partially -- the frustrated -1/3,
    // far from the -1 of an unfrustrated antiferromagnet.
    EXPECT_NEAR(c01, -1.0 / 3.0, 0.15);
    EXPECT_NEAR(c12, -1.0 / 3.0, 0.15);
    EXPECT_NEAR(c02, -1.0 / 3.0, 0.15);
}
