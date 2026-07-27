// tests/cognition/loopy_bp_tests.cpp
//
// The chi = 1 LOOPY backend: self-consistent mean field with message DAMPING on
// a CYCLIC coordination graph. It is NOT exact on loops (nor even on a quantum
// tree -- chi = 1 is a product-state approximation), so it is cross-checked here
// QUALITATIVELY against the exact village oracle (tests/cognition/
// village_tests.cpp): a ferro chain commits together, an even AFM cycle
// alternates cleanly (Neel order), and an odd AFM cycle FRUSTRATES (cannot
// cleanly satisfy). A QUANTITATIVE match to the oracle's numbers (the triangle's
// -1/3 correlation, the cat-state <sigma_z> = 0) is a finite-chi (Seam 3) goal,
// as is the proper cavity / exclude-the-target BP refinement with the
// M * M_reverse message normalization.
//
// The one thing loops force that trees do not: on a frustrated loop the undamped
// Jacobi fixed-point iteration oscillates. Test 2 pins down that DAMPING tames
// it (a converged fixed point, materially tighter than the undamped residual).

#include <cognition/loopy_bp.h>

#include <cognition/anneal.h>
#include <cognition/exact_group.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace wz::engine::cognition;

namespace
{
    // The shared step-count anneal driver (cognition/anneal.h), which now takes
    // any backend -- this file used to carry its own copy of the gamma ramp
    // because the library's anneal() had hand-written overloads for exactly two
    // backends and LoopyBpGroup was not one of them.
    void anneal(
        LoopyBpGroup& g,
        double gamma_start,
        double gamma_end,
        double dtau,
        uint32_t steps)
    {
        anneal(g, AnnealSchedule{ .gamma_start = gamma_start,
            .gamma_end = gamma_end, .dtau = dtau, .steps = steps });
    }

    // Max per-agent |z change| across one relax_step -- the fixed-point residual
    // used to detect oscillation vs convergence.
    double residual_of_one_step(LoopyBpGroup& g, double gamma, double dtau)
    {
        const std::vector<double> before = g.z;
        relax_step(g, gamma, dtau);
        double worst = 0.0;
        for (size_t i = 0; i < g.z.size(); ++i) {
            worst = std::max(worst, std::abs(g.z[i] - before[i]));
        }
        return worst;
    }
}

// ---------------------------------------------------------------------------
// 1. Ferromagnetic chain commits together.
//
// A ferro chain (bonds {i,i+1,+1}) with a small +z goal on agent 0, annealed by
// ramping gamma 2 -> 0. Order propagates through the chain and every agent
// commits to strong +z. The anti-goal flips them all to strong -z. This is the
// tree-like regime where chi = 1 loopy agrees well with the exact backend.
// ---------------------------------------------------------------------------
TEST(LoopyBp, FerromagneticChainCommitsTogether)
{
    const std::vector<ExactBond> chain = {
        ExactBond{ 0, 1, +1.0 },
        ExactBond{ 1, 2, +1.0 },
        ExactBond{ 2, 3, +1.0 },
        ExactBond{ 3, 4, +1.0 },
    };

    LoopyBpGroup g = make_loopy_bp_group(5, chain, /*damping=*/0.5);
    set_goals(g, { Goal{ .agent = 0, .field = 0.05 } });
    anneal(g, /*gamma_start=*/2.0, /*gamma_end=*/0.0, /*dtau=*/0.05, /*steps=*/600);

    const std::vector<double> z = decisions(g);
    for (uint32_t i = 0; i < 5; ++i) {
        EXPECT_GT(z[i], 0.9) << "ferro chain agent " << i << " did not commit +";
    }

    // Anti-goal: the same chain flips wholesale to strong -z.
    LoopyBpGroup gm = make_loopy_bp_group(5, chain, /*damping=*/0.5);
    set_goals(gm, { Goal{ .agent = 0, .field = -0.05 } });
    anneal(gm, 2.0, 0.0, 0.05, 600);
    const std::vector<double> zm = decisions(gm);
    for (uint32_t i = 0; i < 5; ++i) {
        EXPECT_LT(zm[i], -0.9)
            << "anti-goal ferro chain agent " << i << " did not commit -";
    }
}

// ---------------------------------------------------------------------------
// 2. Damping converges a frustrated loop; undamped oscillates.
//
// An all-antiferromagnetic TRIANGLE, iterated at a FIXED frustrated point with a
// near-SATURATING imaginary-time step (large dtau -- each step drives the qubit
// almost fully to sign(h_eff)). This is the regime where the classic loopy-BP
// LIMIT CYCLE appears: undamped Jacobi cannot settle the frustrated loop -- the
// three "everyone must disagree" constraints chase each other around the ring, so
// the broadcasts flip every step (a period-2 oscillation, residual ~ 2, the full
// -1..+1 swing). Message DAMPING (lambda = 0.5) mixes in the previous broadcast,
// makes the update contractive, and drives it to a fixed point (residual ~ 0).
//
// (With a SMALL dtau the imaginary-time step is itself contractive and even the
// undamped iteration converges -- damping only becomes load-bearing once the
// per-step update is near-saturating, which is the interesting frustrated regime.
// See tests 1/3/3b/4, which anneal with the small dtau = 0.05 step.)
//
// The assertion is the contrast: damped residual << undamped residual.
// ---------------------------------------------------------------------------
TEST(LoopyBp, DampingConvergesFrustratedTriangle)
{
    const std::vector<ExactBond> triangle = {
        ExactBond{ 0, 1, -1.0 },
        ExactBond{ 1, 2, -1.0 },
        ExactBond{ 0, 2, -1.0 },
    };
    const double gamma = 0.1;   // near the classical (frustrated) limit
    const double dtau = 2.0;    // near-saturating step -> the limit-cycle regime
    const uint32_t warm = 200;  // long enough for each iteration to settle in

    // Damped: warm up, then measure one more step's residual.
    LoopyBpGroup damped = make_loopy_bp_group(3, triangle, /*damping=*/0.5);
    // A tiny symmetry break so the fixed point is a definite branch.
    set_goals(damped, { Goal{ .agent = 0, .field = 0.01 } });
    for (uint32_t i = 0; i < warm; ++i) {
        relax_step(damped, gamma, dtau);
    }
    const double damped_residual = residual_of_one_step(damped, gamma, dtau);

    // Undamped: identical protocol but damping = 1.0 (pure Jacobi).
    LoopyBpGroup undamped = make_loopy_bp_group(3, triangle, /*damping=*/1.0);
    set_goals(undamped, { Goal{ .agent = 0, .field = 0.01 } });
    for (uint32_t i = 0; i < warm; ++i) {
        relax_step(undamped, gamma, dtau);
    }
    const double undamped_residual = residual_of_one_step(undamped, gamma, dtau);

    // Damped run has reached a fixed point: tiny residual.
    EXPECT_LT(damped_residual, 1e-3)
        << "damped triangle did not converge to a fixed point";
    // Undamped run still oscillates: the frustrated loop's period-2 limit cycle,
    // materially (orders of magnitude) larger than the damped residual.
    EXPECT_GT(undamped_residual, 0.5)
        << "undamped triangle did not oscillate (limit cycle not reached)";
    EXPECT_GT(undamped_residual, 100.0 * damped_residual)
        << "damping did not tame the frustrated-loop oscillation";

    RecordProperty("damped_residual", std::to_string(damped_residual));
    RecordProperty("undamped_residual", std::to_string(undamped_residual));
    std::printf(
        "[ residuals ] damped=%.3e  undamped=%.3e  ratio=%.1fx\n",
        damped_residual, undamped_residual,
        undamped_residual / std::max(damped_residual, 1e-30));
}

// ---------------------------------------------------------------------------
// 3. Satisfiable even cycle alternates (Neel order).
//
// An all-AFM 4-cycle (0-1-2-3-0) with a small +z goal on agent 0, annealed.
// This is BIPARTITE, so it is NOT frustrated: adjacent agents end opposite-
// signed (anti-correlated) and opposite corners same-signed. Mean field
// represents this cleanly -- exactly where chi = 1 loopy agrees with the oracle.
// ---------------------------------------------------------------------------
TEST(LoopyBp, SatisfiableEvenCycleAlternates)
{
    const std::vector<ExactBond> quad = {
        ExactBond{ 0, 1, -1.0 },
        ExactBond{ 1, 2, -1.0 },
        ExactBond{ 2, 3, -1.0 },
        ExactBond{ 3, 0, -1.0 },
    };
    LoopyBpGroup g = make_loopy_bp_group(4, quad, /*damping=*/0.5);
    set_goals(g, { Goal{ .agent = 0, .field = 0.05 } });
    anneal(g, 2.0, 0.0, 0.05, 600);

    const std::vector<double> z = decisions(g);

    // Adjacent agents anti-correlate; opposite corners agree.
    EXPECT_LT(z[0] * z[1], 0.0) << "0,1 not anti-aligned";
    EXPECT_LT(z[1] * z[2], 0.0) << "1,2 not anti-aligned";
    EXPECT_LT(z[2] * z[3], 0.0) << "2,3 not anti-aligned";
    EXPECT_LT(z[3] * z[0], 0.0) << "3,0 not anti-aligned";
    EXPECT_GT(z[0] * z[2], 0.0) << "0,2 (opposite corners) not aligned";
    EXPECT_GT(z[1] * z[3], 0.0) << "1,3 (opposite corners) not aligned";

    // The alternation is CLEAN: every agent strongly ordered (near +-1), unlike
    // the frustrated triangle whose agents stay unpolarized.
    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_GT(std::abs(z[i]), 0.75)
            << "even-cycle agent " << i << " not cleanly ordered";
    }
    // Goal anchors agent 0 to +z (the bipartite tie-break).
    EXPECT_GT(z[0], 0.75);
}

// ---------------------------------------------------------------------------
// 3b. Frustration leaves the triangle with an UNSATISFIED edge.
//
// The AFM triangle with a small +z goal on agent 0, annealed to gamma -> 0.
// Agent 0 follows its goal to +z. The other two both go negative to anti-align
// with agent 0 -- which cleanly satisfies edges 0-1 and 0-2, but leaves agents 1
// and 2 AGREEING (both negative), so THEIR antiferromagnetic edge (1-2) is
// UNSATISFIED. A triangle cannot satisfy all three "disagree" edges at once;
// mean field breaks the symmetry and sacrifices exactly one edge. That
// sacrificed edge -- a pair FORCED to agree despite an AFM bond -- is the chi = 1
// manifestation of the exact oracle's frustration (the exact backend instead
// spreads the frustration as the -1/3 partial correlation with unpolarized
// agents; the two agree QUALITATIVELY that the triangle is frustrated where the
// even cycle is not).
//
// Contrast: the satisfiable even cycle sacrifices NO edge -- all four are cleanly
// anti-satisfied. Robust form: the triangle has a frustrated edge (its worst
// edge anti-alignment is negative -- a pair agrees), while EVERY even-cycle edge
// is cleanly anti-satisfied.
// ---------------------------------------------------------------------------
TEST(LoopyBp, FrustratedTriangleHasAnUnsatisfiedEdge)
{
    const std::vector<ExactBond> triangle = {
        ExactBond{ 0, 1, -1.0 },
        ExactBond{ 1, 2, -1.0 },
        ExactBond{ 0, 2, -1.0 },
    };
    LoopyBpGroup tri = make_loopy_bp_group(3, triangle, /*damping=*/0.5);
    set_goals(tri, { Goal{ .agent = 0, .field = 0.05 } });
    anneal(tri, 2.0, 0.0, 0.05, 600);
    const std::vector<double> zt = decisions(tri);

    // Agent 0 follows its goal.
    EXPECT_GT(zt[0], 0.5) << "goal-anchored agent 0 did not commit +";

    // Per-edge anti-alignment: -(z_a * z_b) > 0 means the AFM edge is satisfied
    // (opposite signs), < 0 means it is FRUSTRATED (the pair agrees).
    const double e01 = -(zt[0] * zt[1]);
    const double e12 = -(zt[1] * zt[2]);
    const double e02 = -(zt[0] * zt[2]);
    const double tri_worst = std::min({ e01, e12, e02 });

    // The triangle cannot satisfy all three: its worst edge is frustrated -- the
    // sacrificed pair AGREES despite an AFM bond (anti-alignment clearly < 0).
    EXPECT_LT(tri_worst, -0.25)
        << "triangle satisfied all three AFM edges (impossible -- no frustration "
           "signature)";

    // Even-cycle reference: it sacrifices no edge.
    const std::vector<ExactBond> quad = {
        ExactBond{ 0, 1, -1.0 }, ExactBond{ 1, 2, -1.0 },
        ExactBond{ 2, 3, -1.0 }, ExactBond{ 3, 0, -1.0 },
    };
    LoopyBpGroup q = make_loopy_bp_group(4, quad, /*damping=*/0.5);
    set_goals(q, { Goal{ .agent = 0, .field = 0.05 } });
    anneal(q, 2.0, 0.0, 0.05, 600);
    const std::vector<double> zq = decisions(q);
    const double q01 = -(zq[0] * zq[1]);
    const double q12 = -(zq[1] * zq[2]);
    const double q23 = -(zq[2] * zq[3]);
    const double q30 = -(zq[3] * zq[0]);
    const double quad_worst = std::min({ q01, q12, q23, q30 });

    // Every even-cycle edge is cleanly anti-satisfied -- no sacrificed edge.
    EXPECT_GT(quad_worst, 0.75)
        << "even cycle failed to satisfy an edge (should be frustration-free)";

    // Categorical gap: the even cycle's WORST edge beats the triangle's worst by
    // a wide margin -- frustration is not a numerical accident.
    EXPECT_GT(quad_worst, tri_worst + 0.5);

    std::printf(
        "[ triangle ] z=(%.3f, %.3f, %.3f)  edges anti (01,12,02)="
        "(%.3f, %.3f, %.3f)  worst=%.3f  even-cycle worst=%.3f\n",
        zt[0], zt[1], zt[2], e01, e12, e02, tri_worst, quad_worst);
}

// ---------------------------------------------------------------------------
// 4. Collapse pins and conditions neighbors.
//
// A ferro pair (2 agents, j = +1). Anneal partway, then collapse agent 0 to
// |1> (bit = true). After more relaxation, agent 0 stays pinned near -1 (does
// not drift), and agent 1 is dragged toward -1 by the ferro bond to the pinned
// neighbor. Collapsing instead to |0> drags agent 1 toward +1.
// ---------------------------------------------------------------------------
TEST(LoopyBp, CollapsePinsAndConditionsNeighbor)
{
    const std::vector<ExactBond> pair = { ExactBond{ 0, 1, +1.0 } };

    // Collapse to |1>: agent 1 dragged toward -1.
    {
        LoopyBpGroup g = make_loopy_bp_group(2, pair, /*damping=*/0.5);
        anneal(g, 2.0, 0.5, 0.05, 200);  // partway (leave gamma up a bit)
        collapse(g, 0, /*bit=*/true);    // agent 0 -> |1> (z = -1)
        anneal(g, 0.5, 0.0, 0.05, 400);  // finish annealing

        EXPECT_LT(decision_z(g, 0), -0.99) << "pinned agent 0 drifted off |1>";
        EXPECT_LT(decision_z(g, 1), -0.5)
            << "agent 1 not dragged toward the pinned neighbor";
    }

    // Collapse to |0>: agent 1 dragged toward +1.
    {
        LoopyBpGroup g = make_loopy_bp_group(2, pair, /*damping=*/0.5);
        anneal(g, 2.0, 0.5, 0.05, 200);
        collapse(g, 0, /*bit=*/false);   // agent 0 -> |0> (z = +1)
        anneal(g, 0.5, 0.0, 0.05, 400);

        EXPECT_GT(decision_z(g, 0), 0.99) << "pinned agent 0 drifted off |0>";
        EXPECT_GT(decision_z(g, 1), 0.5)
            << "agent 1 not dragged toward the pinned neighbor";
    }
}

// ---------------------------------------------------------------------------
// 5. decision_z / decisions contract smoke.
//
// On a small group: every decision is in [-1, 1] and decisions() matches the
// per-agent decision_z reads element for element.
// ---------------------------------------------------------------------------
TEST(LoopyBp, DecisionContractSmoke)
{
    const std::vector<ExactBond> chain = {
        ExactBond{ 0, 1, +1.0 },
        ExactBond{ 1, 2, -1.0 },
    };
    LoopyBpGroup g = make_loopy_bp_group(3, chain, /*damping=*/0.5);
    set_goals(g, { Goal{ .agent = 0, .field = 0.1 } });
    anneal(g, 2.0, 0.0, 0.05, 300);

    const std::vector<double> z = decisions(g);
    ASSERT_EQ(z.size(), 3u);
    for (uint32_t i = 0; i < 3; ++i) {
        EXPECT_GE(z[i], -1.0);
        EXPECT_LE(z[i], 1.0);
        EXPECT_DOUBLE_EQ(z[i], decision_z(g, i))
            << "decisions()[" << i << "] != decision_z(" << i << ")";
    }
}
