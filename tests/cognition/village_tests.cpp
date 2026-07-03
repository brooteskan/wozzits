// tests/cognition/village_tests.cpp
//
// VILLAGE = a coordination group whose bond graph is CYCLIC (girth >= 3): a
// loop of three or more agents, not just a tree/chain. This group is the exact
// ORACLE for the future Tier-2 loopy-BP backend (Tier 2 Seam 2): a village of
// up to ~12 qubits fits the exact joint-state backend, so its relaxed marginals
// and pair correlations are ground truth. When the loopy-BP backend lands it
// must reproduce these same numbers on the same cyclic villages -- this file
// pins them down first.
//
// Two things are locked here:
//   1. The store path (AgentCognitionStore) canonicalizes redundant/malformed
//      bonds before dispatching to a backend, so an author's messy village graph
//      and its clean canonical form drive the same decision. (Change 1's choke
//      point.)
//   2. The frustration signature of an ODD cyclic village (partial anti-
//      correlation) versus the clean satisfaction of an EVEN one -- the drama
//      primitive the loopy backend must eventually preserve.

#include <cognition/agent_cognition.h>
#include <cognition/anneal.h>
#include <cognition/commit.h>
#include <cognition/exact_group.h>

#include <gtest/gtest.h>

#include <cmath>
#include <optional>
#include <vector>

using namespace wz::engine::cognition;

namespace
{
    CognitionClock anneal_clock()
    {
        CognitionClock clock;
        clock.gamma_start = 3.0;
        clock.gamma_end = 0.0;
        clock.anneal_seconds = 4.0;
        clock.relax_rate = 1.0;
        clock.max_substep = 0.05;
        return clock;
    }

    // Drive an agent through a full anneal: start at 0, wake every 0.1s out to
    // t=8 (past the sweep into the committed hold). Matches the pattern the
    // agent_cognition tests use.
    void run_anneal(AgentCognitionStore& store, AgentHandle h)
    {
        store.start(h, 0.0);
        for (int i = 1; i <= 80; ++i) {
            store.think(h, 0.1 * i);
        }
    }

    // Build one triangle-village agent on the exact backend with the given raw
    // bonds, drive it through an identical anneal, and return its three per-agent
    // marginals. Identical clock / commit / seed across calls, so any difference
    // in the result is purely the bond canonicalization.
    std::vector<double> triangle_marginals(const std::vector<ExactBond>& bonds)
    {
        AgentCognitionStore store;
        AgentSpec spec;
        spec.agent_count = 3;
        spec.bonds = bonds;
        // A tiny +z symmetry break on agent 0 so the frustrated village settles to
        // a definite, reproducible branch rather than a symmetric mixture (keeps
        // the two runs comparable term-by-term). Deterministic; same for both.
        spec.goals = { Goal{ .agent = 0, .field = 0.05 } };
        spec.clock = anneal_clock();
        spec.commit = CommitPolicy{ .confidence = 0.9, .decoherence_rate = 0.0 };
        spec.chi = 0;  // exact -- a village is cyclic, not a TTN chain
        spec.seed = 0xC0FFEEu;

        const AgentHandle h = store.create(spec);
        EXPECT_NE(h, kInvalidAgent);
        run_anneal(store, h);

        std::vector<double> z(3, 0.0);
        for (uint32_t i = 0; i < 3; ++i) {
            z[i] = store.marginal(h, i);
        }
        return z;
    }

    AnnealSchedule village_schedule()
    {
        // Ramp Gamma from exploratory down to committed, enough steps for a small
        // village to settle. Mirrors the exact/goals tests' relaxation depth.
        AnnealSchedule s;
        s.gamma_start = 2.0;
        s.gamma_end = 0.0;
        s.dtau = 0.05;
        s.steps = 600;
        return s;
    }
}

// ---------------------------------------------------------------------------
// 1. The store canonicalizes redundant input.
//
// Agent A is authored with a MESSY triangle village: a duplicated/parallel edge
// {0,1} split as two 0.5 halves in opposite orientation, plus an inert self-bond
// on agent 2. Agent B is the equivalent CLEAN village: one {0,1,1.0} edge and no
// self-bond. Canonicalization at the store's build choke point must fold A into
// B, so both agents reach the same per-agent marginals under an identical anneal.
// ---------------------------------------------------------------------------
TEST(Village, StoreCanonicalizesRedundantInput)
{
    // RAW: parallel {0,1} halves (one written (1,0) to also exercise orientation)
    // + a self-bond on agent 2 (dynamically inert) + the two other triangle edges.
    const std::vector<ExactBond> raw = {
        ExactBond{ 0, 1, 0.5 },
        ExactBond{ 1, 0, 0.5 },   // parallel to the above -> sums to {0,1,1.0}
        ExactBond{ 2, 2, 1.0 },   // self-bond -> dropped
        ExactBond{ 1, 2, 1.0 },
        ExactBond{ 0, 2, 1.0 },
    };
    // CANONICAL equivalent: the summed {0,1} edge + the two real edges, no self-bond.
    const std::vector<ExactBond> canonical = {
        ExactBond{ 0, 1, 1.0 },
        ExactBond{ 1, 2, 1.0 },
        ExactBond{ 0, 2, 1.0 },
    };

    const std::vector<double> za = triangle_marginals(raw);
    const std::vector<double> zb = triangle_marginals(canonical);

    for (uint32_t i = 0; i < 3; ++i) {
        EXPECT_NEAR(za[i], zb[i], 1e-9)
            << "agent " << i << ": store did not canonicalize raw village to clean";
    }

    // And specifically: adding an inert self-bond must not change the outcome vs
    // no self-bond at all (self-coupling is a constant energy shift).
    const std::vector<ExactBond> with_self = {
        ExactBond{ 0, 1, 1.0 },
        ExactBond{ 1, 2, 1.0 },
        ExactBond{ 0, 2, 1.0 },
        ExactBond{ 1, 1, 2.0 },   // inert self-bond
    };
    const std::vector<double> zc = triangle_marginals(with_self);
    for (uint32_t i = 0; i < 3; ++i) {
        EXPECT_NEAR(zc[i], zb[i], 1e-9)
            << "agent " << i << ": self-bond changed the village outcome";
    }
}

// ---------------------------------------------------------------------------
// 2. Frustrated odd cycle -- the drama primitive.
//
// An all-antiferromagnetic odd loop cannot satisfy every edge at once: the three
// (or five) "everyone should disagree with their neighbours" constraints conflict
// around the ring. The relaxed state is the classic frustrated compromise --
// every pair only PARTIALLY anti-correlated, each agent unpolarized, nowhere near
// the clean -1 an unfrustrated antiferromagnetic pair reaches.
// ---------------------------------------------------------------------------
TEST(Village, FrustratedTriangleIsPartiallyAntiCorrelated)
{
    ExactGroup g = make_exact_group(
        3, { ExactBond{ 0, 1, -1.0 }, ExactBond{ 1, 2, -1.0 },
             ExactBond{ 0, 2, -1.0 } });
    anneal(g, village_schedule());

    const double c01 = connected_correlation(g, 0, 1);
    const double c12 = connected_correlation(g, 1, 2);
    const double c02 = connected_correlation(g, 0, 2);

    // Frustration signature: anti-correlated but only PARTIALLY (the classic
    // -1/3), not the -1 of a satisfiable antiferromagnet.
    for (double c : { c01, c12, c02 }) {
        EXPECT_LT(c, 0.0);              // still leans anti
        EXPECT_GT(c, -0.75);           // but far short of a clean -1 (frustrated)
    }
    EXPECT_NEAR(c01, -1.0 / 3.0, 0.15);
    EXPECT_NEAR(c12, -1.0 / 3.0, 0.15);
    EXPECT_NEAR(c02, -1.0 / 3.0, 0.15);

    // And each agent is essentially unpolarized -- no clean disposition exists.
    for (uint32_t i = 0; i < 3; ++i) {
        EXPECT_NEAR(decision_z(g, i), 0.0, 0.2);
    }
}

TEST(Village, FrustratedFiveCycleCannotCleanlySatisfy)
{
    // All-AFM 5-cycle: 0-1-2-3-4-0. An odd ring, so still frustrated -- no pair
    // reaches a clean +-1 satisfaction.
    ExactGroup g = make_exact_group(
        5, { ExactBond{ 0, 1, -1.0 }, ExactBond{ 1, 2, -1.0 },
             ExactBond{ 2, 3, -1.0 }, ExactBond{ 3, 4, -1.0 },
             ExactBond{ 4, 0, -1.0 } });
    anneal(g, village_schedule());

    // Every ring edge leans anti but none is cleanly satisfied.
    const uint32_t edges[5][2] = { {0,1}, {1,2}, {2,3}, {3,4}, {4,0} };
    for (const auto& e : edges) {
        const double c = connected_correlation(g, e[0], e[1]);
        EXPECT_LT(c, 0.0);      // anti-correlated
        EXPECT_GT(c, -0.9);     // but frustrated: short of a clean -1
    }
    // Unpolarized agents (frustrated ring has no definite ground disposition).
    for (uint32_t i = 0; i < 5; ++i) {
        EXPECT_NEAR(decision_z(g, i), 0.0, 0.25);
    }
}

// ---------------------------------------------------------------------------
// 3. Satisfiable even cycle -- the contrast.
//
// An all-AFM 4-cycle (0-1-2-3-0) is BIPARTITE, so it is NOT frustrated: the
// alternating pattern (0,2) vs (1,3) satisfies every edge. After annealing Gamma
// down, adjacent pairs anti-correlate STRONGLY (|c| close to 1, materially
// beyond the frustrated triangle's 1/3), and the two opposite-corner pairs --
// same sublattice -- correlate POSITIVELY.
// ---------------------------------------------------------------------------
TEST(Village, SatisfiableFourCycleAntiCorrelatesStrongly)
{
    ExactGroup g = make_exact_group(
        4, { ExactBond{ 0, 1, -1.0 }, ExactBond{ 1, 2, -1.0 },
             ExactBond{ 2, 3, -1.0 }, ExactBond{ 3, 0, -1.0 } });
    anneal(g, village_schedule());

    const double c01 = connected_correlation(g, 0, 1);
    const double c12 = connected_correlation(g, 1, 2);
    const double c23 = connected_correlation(g, 2, 3);
    const double c30 = connected_correlation(g, 3, 0);
    // Opposite corners share a sublattice in the alternating ground state.
    const double c02 = connected_correlation(g, 0, 2);
    const double c13 = connected_correlation(g, 1, 3);

    // Adjacent pairs are strongly anti-correlated (satisfiable, so near -1).
    for (double c : { c01, c12, c23, c30 }) {
        EXPECT_LT(c, -0.75);
    }
    // Opposite corners agree (positive correlation) -- same sublattice.
    EXPECT_GT(c02, 0.5);
    EXPECT_GT(c13, 0.5);
}

// The direct head-to-head the loopy backend will inherit: the satisfiable even
// cycle reaches MATERIALLY stronger edge anti-correlation than the frustrated
// triangle does. Frustration is not a numerical accident -- it is a categorical
// gap between the odd (frustrated) and even (satisfiable) villages.
TEST(Village, EvenCycleBeatsFrustratedTriangleOnEdgeAntiCorrelation)
{
    ExactGroup tri = make_exact_group(
        3, { ExactBond{ 0, 1, -1.0 }, ExactBond{ 1, 2, -1.0 },
             ExactBond{ 0, 2, -1.0 } });
    anneal(tri, village_schedule());

    ExactGroup quad = make_exact_group(
        4, { ExactBond{ 0, 1, -1.0 }, ExactBond{ 1, 2, -1.0 },
             ExactBond{ 2, 3, -1.0 }, ExactBond{ 3, 0, -1.0 } });
    anneal(quad, village_schedule());

    const double tri_edge = std::abs(connected_correlation(tri, 0, 1));
    const double quad_edge = std::abs(connected_correlation(quad, 0, 1));

    // The even cycle's edge is much more anti-correlated than the frustrated
    // triangle's ~1/3 -- a materially larger magnitude, not a marginal one.
    EXPECT_GT(quad_edge, tri_edge + 0.3);
    EXPECT_LT(tri_edge, 0.5);    // triangle stuck near the frustrated 1/3
    EXPECT_GT(quad_edge, 0.75);  // even cycle cleanly satisfied
}
