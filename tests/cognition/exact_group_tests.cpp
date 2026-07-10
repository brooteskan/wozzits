#include <cognition/exact_group.h>

#include <gtest/gtest.h>

#include <algorithm>
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

// Collapsing one agent of an entangled cat CONDITIONS its partner. On the pure
// ferromagnetic cat (|00>+|11>)/sqrt2 both agents are unpolarized, but projecting
// agent 0 onto |1> forces agent 1 to |1> too -- the mechanism that keeps coupled
// commits jointly consistent (no probability-zero 01/10 outcome).
TEST(ExactGroup, CollapseConditionsEntangledPartner)
{
    ExactGroup g = make_exact_group(2, { ExactBond{ .a = 0, .b = 1, .j = 1.0 } });
    relax(g, /*gamma=*/0.1, /*dtau=*/0.05, /*iterations=*/400);
    ASSERT_NEAR(decision_z(g, 1), 0.0, 0.05);   // undecided before the collapse

    collapse(g, /*agent=*/0, /*bit=*/true);     // agent 0 -> |1>
    EXPECT_LT(decision_z(g, 0), -0.99);         // agent 0 now definitely |1>
    EXPECT_LT(decision_z(g, 1), -0.95);         // partner dragged to |1> (ferro)
                                                // (not exactly -1: finite-gamma cat)
    // decisions() (the bulk read) agrees.
    const std::vector<double> z = decisions(g);
    EXPECT_LT(z[0], -0.99);
    EXPECT_LT(z[1], -0.95);
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

// The relaxation step is a 2nd-order symmetric (Strang) Trotter split, so the
// Trotter error of reaching a fixed total imaginary time T scales as O(dtau^2):
// halving dtau (doubling the step count) should cut the error by ~4x. We verify
// this WITHOUT an eigensolver by building a near-exact reference: the same total
// time T reached with a huge number of tiny steps (as dtau->0 the Trotter error
// of ANY order vanishes, so this is effectively the exact e^{-H T} result).
//
// A 1st-order split would only give a ~2x error reduction per halving:
// error(N)/error(2N) ~ 4 for 2nd order vs ~2 for 1st order. We compare N=8 vs
// N=16 (dtau halved once), so the ratio distinguishes the two -- ~4 for the
// symmetric split we just installed, ~2 for the old asymmetric one. We assert
// the error shrinks and the ratio is comfortably above the 1st-order value and
// near the 2nd-order value of 4 (loose band 3.0..5.0).
//
// The terms genuinely do not commute (transverse gamma*sigma_x, longitudinal
// h*sigma_z, and a ZZ coupling), which is what makes the Trotter error nonzero
// and order-dependent in the first place. The longitudinal goal field breaks the
// z-symmetry, so no random seed is needed (the relaxation is deterministic).
namespace
{
    // Relax a fresh 2-qubit non-commuting group to total imaginary time T using
    // `steps` equal Trotter steps, and return agent 0's decision <sigma_z>.
    double relaxed_decision(double total_time, uint32_t steps)
    {
        ExactGroup g = make_exact_group(2, { ExactBond{ .a = 0, .b = 1, .j = 0.5 } });
        set_goals(g, { Goal{ .agent = 0, .field = 0.3 } });
        relax(g, /*gamma=*/1.0, /*dtau=*/total_time / steps, /*iterations=*/steps);
        return decision_z(g, 0);
    }
}

TEST(ExactGroup, SymmetricTrotterStepIsSecondOrder)
{
    const double T = 1.0;

    // Near-exact reference: many tiny steps drive the Trotter error to ~0.
    const double ref = relaxed_decision(T, /*steps=*/2000);

    const double z8 = relaxed_decision(T, /*steps=*/8);
    const double z16 = relaxed_decision(T, /*steps=*/16);

    const double err8 = std::abs(z8 - ref);
    const double err16 = std::abs(z16 - ref);

    // Error must actually shrink as dtau shrinks.
    EXPECT_GT(err8, err16);
    EXPECT_GT(err8, 0.0);

    // 2nd order => halving dtau cuts the error ~4x. 1st order would give ~2x.
    // Require comfortably above the 1st-order value; expect ~4 (measured ~3.98
    // for this case: err8 ~ 1.5e-4, err16 ~ 3.7e-5).
    const double ratio = err8 / err16;
    EXPECT_GT(ratio, 3.0);
    EXPECT_LT(ratio, 5.0);
}

// make_exact_group must DROP a bond with an out-of-range endpoint. relax_step applies
// each bond's ZZ UNCHECKED, so an out-of-range endpoint would address a non-existent
// qubit (its bit reads a constant 0 -> the bond degenerates into a phantom single-site
// field) and, for an index >= 64, shift-UB. Only the valid bonds survive, and relax
// runs cleanly.
TEST(ExactGroup, DropsOutOfRangeBonds)
{
    // 2-qubit group, but two bonds reference qubits that do not exist.
    ExactGroup g = make_exact_group(
        2, { ExactBond{ .a = 0, .b = 1, .j = 1.0 },     // valid
             ExactBond{ .a = 0, .b = 2, .j = 1.0 },     // b = 2 out of range
             ExactBond{ .a = 99, .b = 1, .j = 1.0 } }); // a = 99 out of range
    EXPECT_EQ(g.bonds.size(), 1u);   // only the valid bond survives

    // No UB, and the marginals stay finite and in range.
    for (int i = 0; i < 50; ++i) {
        relax_step(g, /*gamma=*/0.2, /*dtau=*/0.05);
    }
    for (uint32_t q = 0; q < 2; ++q) {
        const double z = decision_z(g, q);
        EXPECT_TRUE(std::isfinite(z));
        EXPECT_GE(z, -1.0001);
        EXPECT_LE(z, 1.0001);
    }
}
