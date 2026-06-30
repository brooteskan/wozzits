#include <engine/cognition/ttn.h>

#include <engine/cognition/exact_group.h>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace wz::cognition;

// At a bond cap large enough that no truncation occurs, the TTN backend
// reproduces the exact joint-state backend's marginals for the same
// transverse-field-Ising Hamiltonian -- evolution and contraction are both
// exact, just in a linear-cost representation.
TEST(Ttn, FullChiMatchesExactBackend)
{
    const double gamma = 0.2;
    const double dtau = 0.05;
    const uint32_t iters = 200;

    // 4-agent chain; mixed nearest-neighbour couplings; a goal on agent 0.
    TtnChain ttn = make_ttn_chain(4, { 1.0, -1.0, 1.0 }, /*chi=*/4);  // chi=4 = full for n=4
    ttn.goal_field[0] = 0.6;
    relax(ttn, gamma, dtau, iters);
    const auto ttn_z = decisions(ttn);

    ExactGroup exact = make_exact_group(
        4, { ExactBond{ 0, 1, 1.0 }, ExactBond{ 1, 2, -1.0 },
            ExactBond{ 2, 3, 1.0 } });
    set_goals(exact, { Goal{ .agent = 0, .field = 0.6 } });
    relax(exact, gamma, dtau, iters);

    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_NEAR(ttn_z[i], decision_z(exact, i), 1e-5) << "agent " << i;
    }
}

// A goal on one agent propagates through ferromagnetic couplings: every agent in
// the chain commits the same way.
TEST(Ttn, GoalPropagatesAlongFerromagneticChain)
{
    TtnChain g = make_ttn_chain(3, { 1.0, 1.0 }, /*chi=*/2);
    g.goal_field[0] = 0.5;
    relax(g, /*gamma=*/0.1, /*dtau=*/0.05, /*iterations=*/300);

    const auto z = decisions(g);
    EXPECT_GT(z[0], 0.8);
    EXPECT_GT(z[1], 0.8);
    EXPECT_GT(z[2], 0.8);
}

// The truncated backend runs on a longer chain at a small bond cap and yields
// well-formed marginals (the chi dial is bounded-cost, not exponential).
TEST(Ttn, SmallChiRunsAndStaysBounded)
{
    TtnChain g = make_ttn_chain(6, { 1.0, 1.0, 1.0, 1.0, 1.0 }, /*chi=*/2);
    g.goal_field[0] = 0.5;
    relax(g, 0.1, 0.05, 200);

    const auto z = decisions(g);
    ASSERT_EQ(z.size(), 6u);
    for (double v : z) {
        EXPECT_GE(v, -1.0001);
        EXPECT_LE(v, 1.0001);
    }
    EXPECT_GT(z[0], 0.0);  // the goal still biases the chain
}
