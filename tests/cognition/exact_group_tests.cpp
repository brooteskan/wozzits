#include <cognition/exact_group.h>

#include <gtest/gtest.h>

#include <cmath>

using namespace wz::engine::cognition;

// Ferromagnetic group relaxes to the entangled cat state: each agent is
// UNPOLARIZED (<sigma_z> ~ 0) yet the pair is PERFECTLY CORRELATED (connected
// correlation ~ +1). Correlation without polarization is genuine entanglement --
// the mean-field backend (which always picks a definite aligned <sigma_z>)
// structurally cannot produce this.
TEST(ExactGroup, FerromagneticPairIsEntangledCat)
{
    ExactGroup g = make_exact_group(2, { ExactBond{ .a = 0, .b = 1, .j = 1.0 } });
    relax(g, /*gamma=*/0.1, /*dtau=*/0.05, /*iterations=*/400);

    EXPECT_NEAR(decision_z(g, 0), 0.0, 0.05);
    EXPECT_NEAR(decision_z(g, 1), 0.0, 0.05);
    EXPECT_GT(connected_correlation(g, 0, 1), 0.95);
}

TEST(ExactGroup, AntiferromagneticPairIsAnticorrelated)
{
    ExactGroup g = make_exact_group(2, { ExactBond{ .a = 0, .b = 1, .j = -1.0 } });
    relax(g, 0.1, 0.05, 400);

    EXPECT_NEAR(decision_z(g, 0), 0.0, 0.05);
    EXPECT_NEAR(decision_z(g, 1), 0.0, 0.05);
    EXPECT_LT(connected_correlation(g, 0, 1), -0.95);
}

// A strong transverse field overwhelms the coupling: the group decorrelates
// toward a product paramagnet (no correlation, no polarization).
TEST(ExactGroup, StrongTransverseFieldDecorrelates)
{
    ExactGroup g = make_exact_group(2, { ExactBond{ .a = 0, .b = 1, .j = 0.5 } });
    relax(g, /*gamma=*/3.0, 0.05, 400);

    EXPECT_LT(std::abs(connected_correlation(g, 0, 1)), 0.15);
    EXPECT_LT(std::abs(decision_z(g, 0)), 0.15);
}

// A three-agent ferromagnetic chain (0-1-2) relaxes to a GHZ-like cat
// (|000> + |111>)/sqrt2: every PAIR is correlated -- including 0 and 2, which
// share no direct bond. The entanglement spans the whole group.
TEST(ExactGroup, ChainEntanglesAllPairsIncludingNonAdjacent)
{
    ExactGroup g = make_exact_group(
        3, { ExactBond{ 0, 1, 1.0 }, ExactBond{ 1, 2, 1.0 } });
    relax(g, 0.1, 0.05, 400);

    EXPECT_NEAR(decision_z(g, 0), 0.0, 0.05);
    EXPECT_NEAR(decision_z(g, 1), 0.0, 0.05);
    EXPECT_NEAR(decision_z(g, 2), 0.0, 0.05);
    EXPECT_GT(connected_correlation(g, 0, 1), 0.95);
    EXPECT_GT(connected_correlation(g, 1, 2), 0.95);
    EXPECT_GT(connected_correlation(g, 0, 2), 0.95);  // non-adjacent, still correlated
}
