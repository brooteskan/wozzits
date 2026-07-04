#include <cognition/graph_bp.h>

#include <cognition/exact_group.h>
#include <cognition/graph_tn.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace wz::engine::cognition;

namespace
{
    // Build a graph_tn on the given bonds/goals/Trotter settings, relax it, and
    // return it (the tests read BP marginals off the relaxed state).
    GraphTn relaxed_tn(
        uint32_t agent_count, const std::vector<ExactBond>& bonds,
        const std::vector<Goal>& goals, uint32_t chi, double gamma, double dtau,
        uint32_t iters)
    {
        GraphTn g = make_graph_tn(agent_count, bonds, chi);
        set_goals(g, goals);
        relax(g, gamma, dtau, iters);
        return g;
    }

    // The exact oracle on the same bonds/goals/Trotter settings.
    std::vector<double> exact_decisions(
        uint32_t agent_count, const std::vector<ExactBond>& bonds,
        const std::vector<Goal>& goals, double gamma, double dtau, uint32_t iters)
    {
        ExactGroup e = make_exact_group(agent_count, bonds);
        set_goals(e, goals);
        relax(e, gamma, dtau, iters);
        return decisions(e);
    }
}

// 1. BP is EXACT on a tree. A 5-agent chain (a tree) with mixed ferro/anti and
//    goals, relaxed at generous chi: converge_bp then bp_decisions must match the
//    exact-group oracle to ~1e-6.
TEST(GraphBp, BpMatchesExactOnChain)
{
    const std::vector<ExactBond> bonds = {
        ExactBond{ 0, 1, 1.0 },
        ExactBond{ 1, 2, -0.7 },
        ExactBond{ 2, 3, 0.5 },
        ExactBond{ 3, 4, -1.0 },
    };
    const std::vector<Goal> goals = {
        Goal{ .agent = 0, .field = 0.4 },
        Goal{ .agent = 4, .field = -0.3 },
    };
    const double gamma = 0.5, dtau = 0.05;
    const uint32_t iters = 400;

    const GraphTn g =
        relaxed_tn(5, bonds, goals, /*chi=*/16, gamma, dtau, iters);
    BpConvergence conv;
    const BpMessages msgs =
        converge_bp(g, /*max_iters=*/500, /*tol=*/1e-12, /*damping=*/0.5, &conv);
    const std::vector<double> zbp = bp_decisions(g, msgs);
    const std::vector<double> ze =
        exact_decisions(5, bonds, goals, gamma, dtau, iters);

    double resid = 0.0;
    ASSERT_EQ(zbp.size(), ze.size());
    for (std::size_t i = 0; i < zbp.size(); ++i) {
        resid = std::max(resid, std::abs(zbp[i] - ze[i]));
        EXPECT_NEAR(zbp[i], ze[i], 1e-6) << "chain agent " << i;
    }
    std::printf("[graph_bp] chain: bp-vs-exact max residual = %.3e "
                "(converged in %u iters, msg residual %.2e)\n",
        resid, conv.iterations, conv.residual);
    EXPECT_TRUE(conv.converged);
}

// 2. BP is exact on a branching tree too. A deg-3 hub star exercises the
//    branching cavity (the hub message folds two other incoming messages), which
//    a chain does not.
TEST(GraphBp, BpMatchesExactOnStar)
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
    const double gamma = 0.5, dtau = 0.05;
    const uint32_t iters = 400;

    const GraphTn g =
        relaxed_tn(4, bonds, goals, /*chi=*/16, gamma, dtau, iters);
    BpConvergence conv;
    const BpMessages msgs =
        converge_bp(g, /*max_iters=*/500, /*tol=*/1e-12, /*damping=*/0.5, &conv);
    const std::vector<double> zbp = bp_decisions(g, msgs);
    const std::vector<double> ze =
        exact_decisions(4, bonds, goals, gamma, dtau, iters);

    double resid = 0.0;
    ASSERT_EQ(zbp.size(), ze.size());
    for (std::size_t i = 0; i < zbp.size(); ++i) {
        resid = std::max(resid, std::abs(zbp[i] - ze[i]));
        EXPECT_NEAR(zbp[i], ze[i], 1e-6) << "star agent " << i;
    }
    std::printf("[graph_bp] star: bp-vs-exact max residual = %.3e "
                "(converged in %u iters, msg residual %.2e)\n",
        resid, conv.iterations, conv.residual);
    EXPECT_TRUE(conv.converged);
}

// 3. Cross-validate the BP contraction against the shipped lambda-gauge readout:
//    on a tree BOTH readouts are exact, so they must agree to ~1e-9. This checks
//    the BP message contraction independent of the exact oracle (they share the
//    same evolved state, so any disagreement is a readout-contraction bug).
TEST(GraphBp, BpEqualsLambdaGaugeOnTree)
{
    // A star tree (branching, so it exercises the multi-leg cavity fold).
    const std::vector<ExactBond> bonds = {
        ExactBond{ 0, 1, 0.9 },
        ExactBond{ 0, 2, -0.6 },
        ExactBond{ 0, 3, 0.4 },
    };
    const std::vector<Goal> goals = {
        Goal{ .agent = 2, .field = 0.5 },
    };
    const double gamma = 0.5, dtau = 0.05;
    const uint32_t iters = 400;

    GraphTn g = relaxed_tn(4, bonds, goals, /*chi=*/16, gamma, dtau, iters);
    const std::vector<double> zbp = bp_decisions_converged(
        g, /*max_iters=*/500, /*tol=*/1e-12, /*damping=*/0.5);
    const std::vector<double> zlam = decisions(g);  // lambda-gauge readout

    double resid = 0.0;
    ASSERT_EQ(zbp.size(), zlam.size());
    for (std::size_t i = 0; i < zbp.size(); ++i) {
        resid = std::max(resid, std::abs(zbp[i] - zlam[i]));
        EXPECT_NEAR(zbp[i], zlam[i], 1e-9) << "tree agent " << i;
    }
    std::printf("[graph_bp] tree: bp-vs-lambda-gauge max residual = %.3e\n",
        resid);
}

// 4. Damping is LOAD-BEARING on a frustrated loop. An all-antiferro triangle
//    with a symmetry-breaking goal, evolved at chi=2 in the small-gamma regime
//    where the loop-BP fixed point is stiff: damped Jacobi REACHES the tolerance,
//    while pure Jacobi (damping = 1) oscillates and does NOT converge. Also asserts
//    the BP marginals are finite and in [-1, 1].
TEST(GraphBp, BpConvergesOnFrustratedTriangle)
{
    const std::vector<ExactBond> bonds = {
        ExactBond{ 0, 1, -1.0 },
        ExactBond{ 1, 2, -1.0 },
        ExactBond{ 0, 2, -1.0 },
    };
    const std::vector<Goal> goals = { Goal{ .agent = 0, .field = 0.6 } };
    const double gamma = 0.5, dtau = 0.02;
    const uint32_t iters = 600;

    const GraphTn g = relaxed_tn(3, bonds, goals, /*chi=*/2, gamma, dtau, iters);

    // WITH damping: must reach the tolerance within max_iters.
    BpConvergence damped;
    const BpMessages msgs =
        converge_bp(g, /*max_iters=*/2000, /*tol=*/1e-10, /*damping=*/0.5,
            &damped);

    // WITHOUT damping (pure Jacobi): report its residual -- expected NOT to reach
    // the same tolerance in the same budget (it oscillates on the frustrated loop).
    BpConvergence undamped;
    (void)converge_bp(g, /*max_iters=*/2000, /*tol=*/1e-10, /*damping=*/1.0,
        &undamped);

    std::printf("[graph_bp] frustrated triangle: WITH damping -> converged=%d in "
                "%u iters, residual=%.3e;  WITHOUT damping -> converged=%d in %u "
                "iters, residual=%.3e\n",
        damped.converged ? 1 : 0, damped.iterations, damped.residual,
        undamped.converged ? 1 : 0, undamped.iterations, undamped.residual);

    EXPECT_TRUE(damped.converged)
        << "damped BP must reach tolerance on the frustrated loop (residual="
        << damped.residual << ")";
    EXPECT_FALSE(undamped.converged)
        << "pure Jacobi should NOT converge on the frustrated loop -- if it does, "
           "damping is not load-bearing here (residual=" << undamped.residual
        << ")";

    const std::vector<double> zbp = bp_decisions(g, msgs);
    for (std::size_t i = 0; i < zbp.size(); ++i) {
        EXPECT_TRUE(std::isfinite(zbp[i])) << "agent " << i;
        EXPECT_GE(zbp[i], -1.0 - 1e-9) << "agent " << i;
        EXPECT_LE(zbp[i], 1.0 + 1e-9) << "agent " << i;
    }
}

// 5. The epsilon_BP DIAGNOSTIC. On the same frustrated-triangle chi=2 state,
//    MEASURE and report epsilon_BP = max_i |bp[i] - exact[i]| alongside the
//    lambda-gauge error max_i |lambda_gauge[i] - exact[i]|. Both readouts read the
//    SAME over-evolved chi=2 state, so neither is expected to nail exact -- the
//    point is that epsilon_BP is a WELL-DEFINED FINITE quantity (the guard), and
//    the printed number is the diagnostic. A quantitative epsilon_BP improvement
//    awaits 3b-ii (BP-gauged truncation of the EVOLUTION, not the readout).
TEST(GraphBp, EpsilonBpMeasuredOnTriangle)
{
    const std::vector<ExactBond> bonds = {
        ExactBond{ 0, 1, -1.0 },
        ExactBond{ 1, 2, -1.0 },
        ExactBond{ 0, 2, -1.0 },
    };
    const std::vector<Goal> goals = { Goal{ .agent = 0, .field = 0.6 } };
    const double gamma = 0.5, dtau = 0.02;
    const uint32_t iters = 600;

    GraphTn g = relaxed_tn(3, bonds, goals, /*chi=*/2, gamma, dtau, iters);
    const std::vector<double> zbp = bp_decisions_converged(
        g, /*max_iters=*/2000, /*tol=*/1e-10, /*damping=*/0.5);
    const std::vector<double> zlam = decisions(g);  // lambda-gauge readout
    const std::vector<double> ze =
        exact_decisions(3, bonds, goals, gamma, dtau, iters);

    ASSERT_EQ(zbp.size(), 3u);
    ASSERT_EQ(zlam.size(), 3u);
    ASSERT_EQ(ze.size(), 3u);

    double eps_bp = 0.0, eps_lambda = 0.0;
    for (std::size_t i = 0; i < 3; ++i) {
        eps_bp = std::max(eps_bp, std::abs(zbp[i] - ze[i]));
        eps_lambda = std::max(eps_lambda, std::abs(zlam[i] - ze[i]));
    }
    std::printf("[graph_bp] epsilon_BP diagnostic on frustrated triangle: "
                "epsilon_BP (bp vs exact) = %.4f, lambda-gauge error = %.4f\n"
                "           bp    = [% .4f, % .4f, % .4f]\n"
                "           lambda= [% .4f, % .4f, % .4f]\n"
                "           exact = [% .4f, % .4f, % .4f]\n",
        eps_bp, eps_lambda, zbp[0], zbp[1], zbp[2], zlam[0], zlam[1], zlam[2],
        ze[0], ze[1], ze[2]);

    // Guard: epsilon_BP is a well-defined FINITE quantity (loose sanity bound;
    // both readouts read the same over-evolved chi=2 state, so neither is expected
    // to be tight -- 3b-ii fixes the STATE, not the readout).
    EXPECT_TRUE(std::isfinite(eps_bp));
    EXPECT_TRUE(std::isfinite(eps_lambda));
    EXPECT_LE(eps_bp, 2.0);
    EXPECT_LE(eps_lambda, 2.0);
}

// 6. BP TRACKS EXACT on WELL-CONDITIONED loops. The tree tests (1-3) pin the loop
//    message contraction to machine precision, but only qualitatively on a loop
//    (test 4 = convergence + finiteness; test 5 = a loose epsilon_BP guard). This
//    test closes that gap: on genuinely CYCLIC but well-conditioned states -- where
//    BP theory says loop-BP is a good approximation (weak correlations / bipartite
//    cycles) -- the loop BP readout must actually track the exact-group oracle to a
//    tight, MEASURED tolerance. If the loop message path had a contraction bug that
//    the tree oracles (exact to 1e-15) cannot catch, it would surface HERE as a
//    large error on these weakly-coupled loops.
//
//    Cases + measured max |bp - exact| (deterministic; clang-debug, gamma/dtau as
//    below, 600 iters of the anneal, converge_bp damping 0.5 tol 1e-10):
//        weak antiferro triangle (all j=-0.2, goal +0.6 on a0):  9.03e-3
//        weak ferro    triangle (all j=+0.2, goal +0.6 on a0):  7.13e-3
//        even ferro 4-ring (all j=+1, goal +0.5 on a0, g=0.7):  6.25e-3
//    All three sit under 1e-2; the assertion tolerance 3e-2 is ~3x the worst
//    measured value -- comfortable margin over the measured numbers, still an order
//    of magnitude below the ~0.5-0.9 epsilon of the STIFF frustrated triangle, so
//    it cannot be satisfied by a degenerate near-zero readout. (A true loop bug
//    would blow past 3e-2 on these weak loops.) These are properties of the loop
//    message contraction on well-conditioned states -- NOT a claim that simple
//    update is accurate on stiff frustrated loops (it is not; that is 3b-ii).
TEST(GraphBp, BpTracksExactOnWellConditionedLoops)
{
    struct Case
    {
        const char* name;
        uint32_t agents;
        std::vector<ExactBond> bonds;
        std::vector<Goal> goals;
        uint32_t chi;
        double gamma;
        double dtau;
        uint32_t iters;
    };

    const std::vector<Case> cases = {
        // Weak-coupling triangles: BP loop error is O(j^3) small -> BP ~ exact.
        Case{ "weak-antiferro-triangle", 3,
            { ExactBond{ 0, 1, -0.2 }, ExactBond{ 1, 2, -0.2 },
                ExactBond{ 0, 2, -0.2 } },
            { Goal{ .agent = 0, .field = 0.6 } }, /*chi=*/2, /*gamma=*/0.5,
            /*dtau=*/0.02, /*iters=*/600 },
        Case{ "weak-ferro-triangle", 3,
            { ExactBond{ 0, 1, 0.2 }, ExactBond{ 1, 2, 0.2 },
                ExactBond{ 0, 2, 0.2 } },
            { Goal{ .agent = 0, .field = 0.6 } }, /*chi=*/2, /*gamma=*/0.5,
            /*dtau=*/0.02, /*iters=*/600 },
        // Even ferro 4-ring: a bipartite (even) cycle is BP-friendly.
        Case{ "even-ferro-4ring", 4,
            { ExactBond{ 0, 1, 1.0 }, ExactBond{ 1, 2, 1.0 },
                ExactBond{ 2, 3, 1.0 }, ExactBond{ 3, 0, 1.0 } },
            { Goal{ .agent = 0, .field = 0.5 } }, /*chi=*/2, /*gamma=*/0.7,
            /*dtau=*/0.02, /*iters=*/600 },
    };

    // Comfortable margin over the measured worst case (9.03e-3): ~3x, and still an
    // order of magnitude below the stiff-triangle epsilon (~0.5-0.9), so a
    // degenerate near-zero readout could NOT pass this on these polarized states.
    const double kTol = 3e-2;

    for (const Case& c : cases) {
        const GraphTn g = relaxed_tn(c.agents, c.bonds, c.goals, c.chi, c.gamma,
            c.dtau, c.iters);
        BpConvergence conv;
        const BpMessages msgs = converge_bp(
            g, /*max_iters=*/2000, /*tol=*/1e-10, /*damping=*/0.5, &conv);
        const std::vector<double> zbp = bp_decisions(g, msgs);
        const std::vector<double> ze = exact_decisions(
            c.agents, c.bonds, c.goals, c.gamma, c.dtau, c.iters);

        ASSERT_EQ(zbp.size(), ze.size()) << c.name;
        EXPECT_TRUE(conv.converged)
            << c.name << ": BP must converge on a well-conditioned loop (residual="
            << conv.residual << ")";

        double err = 0.0;
        for (std::size_t i = 0; i < zbp.size(); ++i) {
            err = std::max(err, std::abs(zbp[i] - ze[i]));
        }
        std::printf("[graph_bp] well-conditioned loop %-24s bp-vs-exact max err "
                    "= %.4e (converged in %u iters)\n",
            c.name, err, conv.iterations);
        EXPECT_LE(err, kTol) << c.name
            << ": loop BP readout drifted from exact beyond the measured margin -- "
               "if this fires, suspect the loop message contraction";
    }
}
