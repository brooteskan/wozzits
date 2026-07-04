#include <cognition/graph_tn.h>

#include <cognition/exact_group.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace wz::engine::cognition;

namespace
{
    // Run the graph-TN backend and the exact-group oracle on the SAME bonds,
    // goals, and Trotter settings, and return their per-agent decision vectors so
    // a test can compare them element-by-element.
    struct OraclePair
    {
        std::vector<double> tn;
        std::vector<double> exact;
    };

    OraclePair run_both(
        uint32_t agent_count, const std::vector<ExactBond>& bonds,
        const std::vector<Goal>& goals, uint32_t chi, double gamma, double dtau,
        uint32_t iters)
    {
        GraphTn g = make_graph_tn(agent_count, bonds, chi);
        set_goals(g, goals);
        relax(g, gamma, dtau, iters);

        ExactGroup e = make_exact_group(agent_count, bonds);
        set_goals(e, goals);
        relax(e, gamma, dtau, iters);

        OraclePair out;
        out.tn = decisions(g);
        out.exact = decisions(e);
        return out;
    }
}

// 1. A CHAIN is a tree, so Vidal simple update is EXACT there for any chi large
//    enough to hold the entanglement. A 5-agent chain with mixed ferro/anti
//    couplings and goals must reproduce the exact-group marginals to 1e-6 when
//    chi is generous (no truncation).
TEST(GraphTn, ChainMatchesDenseExact)
{
    const std::vector<ExactBond> bonds = {
        ExactBond{ 0, 1, 1.0 },   // ferro
        ExactBond{ 1, 2, -0.7 },  // anti
        ExactBond{ 2, 3, 0.5 },   // ferro
        ExactBond{ 3, 4, -1.0 },  // anti
    };
    const std::vector<Goal> goals = {
        Goal{ .agent = 0, .field = 0.4 },
        Goal{ .agent = 4, .field = -0.3 },
    };

    const OraclePair r = run_both(
        /*agents=*/5, bonds, goals, /*chi=*/16, /*gamma=*/0.5, /*dtau=*/0.05,
        /*iters=*/400);

    ASSERT_EQ(r.tn.size(), r.exact.size());
    for (std::size_t i = 0; i < r.tn.size(); ++i) {
        EXPECT_NEAR(r.tn[i], r.exact[i], 1e-6) << "chain agent " << i;
    }
}

// 2. A STAR (deg-3 hub bonded to three leaves) is still a tree, but -- unlike a
//    chain -- it EXERCISES the lambda-environment absorb/divide-back: the hub has
//    three incident bonds, so gating one requires the other two lambdas folded in
//    as the mean-field environment and divided back out. A chain would pass even
//    with broken environment handling; a branching hub will not. If this fails,
//    the environment logic is wrong -- do NOT loosen the tolerance.
TEST(GraphTn, BranchingStarMatchesExact)
{
    const std::vector<ExactBond> bonds = {
        ExactBond{ 0, 1, 1.0 },
        ExactBond{ 0, 2, -0.8 },
        ExactBond{ 0, 3, 0.6 },
    };
    const std::vector<Goal> goals = {
        Goal{ .agent = 1, .field = 0.3 },
        Goal{ .agent = 3, .field = -0.5 },
    };

    const OraclePair r = run_both(
        /*agents=*/4, bonds, goals, /*chi=*/16, /*gamma=*/0.5, /*dtau=*/0.05,
        /*iters=*/400);

    ASSERT_EQ(r.tn.size(), r.exact.size());
    for (std::size_t i = 0; i < r.tn.size(); ++i) {
        EXPECT_NEAR(r.tn[i], r.exact[i], 1e-6) << "star agent " << i;
    }
}

// 3. An all-antiferromagnetic TRIANGLE is FRUSTRATED: no assignment satisfies all
//    three "disagree with your neighbour" edges. This test discriminates a correct
//    finite-chi ENTANGLED engine from a broken / no-op / product-state one.
//
//    Why the old "no goals" version was VACUOUS: the all-antiferro triangle with
//    no goals is perfectly Z2 spin-flip symmetric, so EVERY agent's <sigma_z> is
//    pinned to 0 by symmetry no matter what the engine computes -- even a no-op
//    engine that returns 0 passes "max|z| small". There was no oracle and no chi
//    contrast, so nothing was actually tested.
//
//    The fix breaks the symmetry with a longitudinal goal (field = +0.6 on agent
//    0). Now the marginals are engine-dependent and non-trivial, so we can pit the
//    SAME bonds/goal/Trotter settings at three fidelities against each other:
//      z1 = graph_tn chi=1  (the product / mean-field limit of THIS engine)
//      z2 = graph_tn chi=2  (bounded entanglement on the loop)
//      ze = ExactGroup       (the chi=infinity oracle)
//    and assert what a correct entangled engine satisfies but a broken/product one
//    cannot:
//      (a) all marginals finite and in [-1, 1];
//      (b) chi=2 DIFFERS MEASURABLY from chi=1 (a no-op engine gives z1==z2, and a
//          product state cannot represent the frustrated loop entanglement);
//      (c) chi=2 is at least as close to the exact oracle as chi=1 -- the genuine
//          "beats mean field" claim, now actually asserted, not assumed.
//
//    Trotter regime matters here: on this frustrated 3-cycle a small transverse
//    field (gamma ~ 0.5) drives the Vidal simple-update to OVER-polarize (chi=2/4
//    both drift to ~+-0.9, worse than chi=1), so (c) would FAIL there. A stronger
//    field (gamma=1.5, dtau=0.02, iters=1000) keeps the state near the paramagnet
//    where the loop-BP fixed point is well-conditioned; there chi=2 tracks the
//    oracle cleanly (and chi=2 ~ chi=4 to 1e-3, i.e. it is converged -- the
//    residual is the loopy-BP approximation, not truncation). Measured at this
//    config (deterministic across repeats):
//      z1 = [ 0.5745, -0.2170, -0.2269]
//      z2 = [ 0.3297, -0.1047, -0.1048]
//      ze = [ 0.4153, -0.1392, -0.1392]
//      max|z2-z1| = 0.2448 (threshold 0.10, 2.4x margin)
//      sum|z1-ze| = 0.3248,  sum|z2-ze| = 0.1545 (chi=2 ~2.1x closer to exact)
TEST(GraphTn, FrustratedTriangleChi2TracksExactBetterThanChi1)
{
    const std::vector<ExactBond> bonds = {
        ExactBond{ 0, 1, -1.0 },
        ExactBond{ 1, 2, -1.0 },
        ExactBond{ 0, 2, -1.0 },
    };
    // Symmetry-breaking goal: without it the Z2-symmetric frustrated point pins
    // every marginal to 0 and the test is vacuous (see comment above).
    const std::vector<Goal> goals = { Goal{ .agent = 0, .field = 0.6 } };
    const double gamma = 1.5, dtau = 0.02;
    const uint32_t iters = 1000;

    GraphTn g1 = make_graph_tn(3, bonds, /*chi=*/1);
    set_goals(g1, goals);
    relax(g1, gamma, dtau, iters);
    const std::vector<double> z1 = decisions(g1);

    GraphTn g2 = make_graph_tn(3, bonds, /*chi=*/2);
    set_goals(g2, goals);
    relax(g2, gamma, dtau, iters);
    const std::vector<double> z2 = decisions(g2);

    ExactGroup e = make_exact_group(3, bonds);
    set_goals(e, goals);
    relax(e, gamma, dtau, iters);
    const std::vector<double> ze = decisions(e);

    ASSERT_EQ(z1.size(), 3u);
    ASSERT_EQ(z2.size(), 3u);
    ASSERT_EQ(ze.size(), 3u);

    // (a) valid marginals at both bond dimensions.
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_TRUE(std::isfinite(z1[i])) << "chi=1 agent " << i;
        EXPECT_TRUE(std::isfinite(z2[i])) << "chi=2 agent " << i;
        EXPECT_GE(z1[i], -1.0 - 1e-9) << "chi=1 agent " << i;
        EXPECT_LE(z1[i], 1.0 + 1e-9) << "chi=1 agent " << i;
        EXPECT_GE(z2[i], -1.0 - 1e-9) << "chi=2 agent " << i;
        EXPECT_LE(z2[i], 1.0 + 1e-9) << "chi=2 agent " << i;
    }

    // (b) chi=2 differs measurably from the chi=1 product limit. A no-op engine
    //     (or one that ignores chi) gives z1 == z2 and fails this. Measured
    //     max|z2-z1| = 0.245; threshold 0.10 leaves comfortable margin.
    double max_diff_21 = 0.0;
    for (std::size_t i = 0; i < 3; ++i) {
        max_diff_21 = std::max(max_diff_21, std::abs(z2[i] - z1[i]));
    }
    EXPECT_GT(max_diff_21, 0.10)
        << "chi=2 must depart from the chi=1 product state on the frustrated "
           "loop; equal marginals mean the entanglement was not represented";

    // (c) the actual "beats mean field" claim: chi=2's total marginal error
    //     against the exact oracle is no worse than chi=1's. Measured
    //     sum|z1-ze| = 0.325, sum|z2-ze| = 0.155 (chi=2 ~2.1x closer).
    double err1 = 0.0, err2 = 0.0;
    for (std::size_t i = 0; i < 3; ++i) {
        err1 += std::abs(z1[i] - ze[i]);
        err2 += std::abs(z2[i] - ze[i]);
    }
    EXPECT_LE(err2, err1 + 1e-9)
        << "chi=2 (sum|z2-ze|=" << err2 << ") should track the exact oracle at "
           "least as well as chi=1 (sum|z1-ze|=" << err1 << ")";
}

// 4a. Truncation error is monotone (non-increasing) in chi on a genuinely
//     entangling case, and vanishes once chi reaches the full bond. A 4-agent
//     ferro RING (a 4-cycle) entangles around the loop, so a small chi truncates.
//     We relax at chi = 1, 2, 4 and read last_truncation_error after a fixed
//     number of steps from the SAME start.
TEST(GraphTn, TruncationErrorMonotoneInChi)
{
    const std::vector<ExactBond> ring = {
        ExactBond{ 0, 1, 1.0 },
        ExactBond{ 1, 2, 1.0 },
        ExactBond{ 2, 3, 1.0 },
        ExactBond{ 3, 0, 1.0 },
    };

    auto trunc_after = [&](uint32_t chi) {
        GraphTn g = make_graph_tn(4, ring, chi);
        // A few steps to build entanglement, then read the last step's error.
        relax(g, /*gamma=*/0.3, /*dtau=*/0.05, /*iters=*/30);
        return g.last_truncation_error;
    };

    const double e1 = trunc_after(1);
    const double e2 = trunc_after(2);
    const double e4 = trunc_after(4);
    // chi = 8 is >= the full Schmidt rank of every cut of a 4-agent single-qubit
    // ring (a bond's rank is bounded by 2^min(sides) <= 4), so no truncation
    // happens and the error is exactly 0.
    const double e8 = trunc_after(8);

    // Strict POSITIVITY at chi = 1 and chi = 2. The old test only checked
    // e >= 0 and e2 <= e1, so it passed VACUOUSLY even if the truncation
    // telemetry were dead (always 0). The 4-cycle ring genuinely entangles
    // around the loop, so a chi = 1 (product) and chi = 2 state both discard
    // real Schmidt weight after 30 steps -- measured e1 ~ 9.96e-3 and
    // e2 ~ 3.64e-6, so the floors below (1e-4 and 1e-8) sit far under the
    // measured values yet still fail a dead/zero telemetry. e4 is NOT asserted
    // positive: chi = 4 can already reach the full bond of a 4-agent ring, so
    // its truncation error is legitimately ~0 (measured ~2e-11).
    EXPECT_GT(e1, 1e-4) << "chi=1 ring must discard Schmidt weight (e1=" << e1
                        << "); a dead telemetry would report 0";
    EXPECT_GT(e2, 1e-8) << "chi=2 ring must discard Schmidt weight (e2=" << e2
                        << "); a dead telemetry would report 0";
    EXPECT_GE(e4, 0.0);
    // More bond dimension keeps more Schmidt weight -> error cannot rise.
    EXPECT_LE(e2, e1 + 1e-12);
    EXPECT_LE(e4, e2 + 1e-12);
    // At chi >= full bond nothing is discarded -> exactly 0.
    EXPECT_NEAR(e8, 0.0, 1e-12);
}

// 4b. The lambda-gauge marginal stays a valid <sigma_z> in [-1, 1] across a relax
//     on an entangling case (a sanity/norm guard on the readout).
TEST(GraphTn, MarginalsStayInRange)
{
    const std::vector<ExactBond> ring = {
        ExactBond{ 0, 1, 1.0 },
        ExactBond{ 1, 2, -1.0 },
        ExactBond{ 2, 3, 1.0 },
        ExactBond{ 3, 0, -1.0 },
    };
    GraphTn g = make_graph_tn(4, ring, /*chi=*/2);
    set_goals(g, { Goal{ .agent = 0, .field = 0.2 } });
    for (int step = 0; step < 200; ++step) {
        relax_step(g, /*gamma=*/0.4, /*dtau=*/0.05);
        for (uint32_t i = 0; i < 4; ++i) {
            const double z = decision_z(g, i);
            EXPECT_GE(z, -1.0 - 1e-9) << "step " << step << " agent " << i;
            EXPECT_LE(z, 1.0 + 1e-9) << "step " << step << " agent " << i;
        }
    }
}

// 5. Collapse pins an agent's decision to the projected bit, and a strongly-
//    ferromagnetic neighbour is dragged toward agreement. Pin agent 0 to |1>
//    (decision_z -> -1); its ferro-bonded neighbour 1 should move toward -1 too.
TEST(GraphTn, CollapseConditions)
{
    const std::vector<ExactBond> bonds = {
        ExactBond{ 0, 1, 2.0 },  // strong ferro
    };
    GraphTn g = make_graph_tn(2, bonds, /*chi=*/4);
    relax(g, /*gamma=*/0.2, /*dtau=*/0.05, /*iters=*/200);

    // Collapse to |0> (bit = false) -> +1.
    {
        GraphTn g0 = make_graph_tn(2, bonds, /*chi=*/4);
        relax(g0, 0.2, 0.05, 200);
        collapse(g0, /*agent=*/0, /*bit=*/false);
        EXPECT_GT(decision_z(g0, 0), 0.99) << "collapse to |0> should pin +1";
        EXPECT_GT(decision_z(g0, 1), 0.5) << "ferro neighbour drags toward +1";
    }
    // Collapse to |1> (bit = true) -> -1.
    {
        collapse(g, /*agent=*/0, /*bit=*/true);
        EXPECT_LT(decision_z(g, 0), -0.99) << "collapse to |1> should pin -1";
        EXPECT_LT(decision_z(g, 1), -0.5) << "ferro neighbour drags toward -1";
    }
}
