#include <cognition/coordination.h>

#include <graph/shared_edge_polytree.h>
#include <cognition/qstate/qstate.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <utility>

using namespace wz::engine::cognition;

// The exact backend drives through the contract: a ferromagnetic pair with a goal
// commits, read uniformly via decision_z(Coordination&, agent).
TEST(Coordination, ExactBackendThroughTheContract)
{
    ExactGroup g = make_exact_group(2, { ExactBond{ 0, 1, 1.0 } });
    set_goals(g, { Goal{ .agent = 0, .field = 0.5 } });
    Coordination c = std::move(g);

    relax(c, /*gamma=*/0.1, /*dtau=*/0.05, /*iterations=*/300);
    EXPECT_GT(decision_z(c, 0), 0.8);
    EXPECT_GT(decision_z(c, 1), 0.8);  // goal propagates through the coupling
}

// The TTN backend drives through the same contract.
TEST(Coordination, TtnBackendThroughTheContract)
{
    TtnChain t = make_ttn_chain(3, { 1.0, 1.0 }, /*chi=*/2);
    t.goal_field[0] = 0.5;
    Coordination c = std::move(t);

    relax(c, 0.1, 0.05, 300);
    EXPECT_GT(decision_z(c, 0), 0.8);
    EXPECT_GT(decision_z(c, 1), 0.8);
    EXPECT_GT(decision_z(c, 2), 0.8);
}

// Fidelity telemetry through the seam: a chi-capped TTN backend reports a
// positive truncation error after relaxing an entangling chain, while the exact
// backend (no truncation) reports exactly 0.
TEST(Coordination, TruncationErrorThroughTheContract)
{
    // chi=1 forces truncation on a ferromagnetic chain -> nonzero telemetry.
    TtnChain t = make_ttn_chain(4, { 1.0, 1.0, 1.0 }, /*chi=*/1);
    Coordination ct = std::move(t);
    relax(ct, /*gamma=*/0.2, /*dtau=*/0.05, /*iterations=*/1);
    EXPECT_GT(truncation_error(ct), 0.0);

    ExactGroup g = make_exact_group(2, { ExactBond{ 0, 1, 1.0 } });
    Coordination ce = std::move(g);
    relax(ce, 0.2, 0.05, 1);
    EXPECT_EQ(truncation_error(ce), 0.0);
}

// The chi = 1 loopy-BP backend drives through the whole contract via the seam:
// relax, decision_z/decisions (in [-1, 1], mutually consistent), collapse (which
// decision_z then reflects), and truncation_error == 0 (chi = 1 never truncates).
TEST(Coordination, LoopyBpBackendThroughTheContract)
{
    // A small ferro chain with a +z goal on agent 0: order propagates and every
    // agent commits +z -- the tree-like regime where chi = 1 agrees with exact.
    LoopyBpGroup g = make_loopy_bp_group(
        3, { ExactBond{ 0, 1, +1.0 }, ExactBond{ 1, 2, +1.0 } });
    set_goals(g, { Goal{ .agent = 0, .field = 0.05 } });
    Coordination c = std::move(g);

    // Anneal gamma 2 -> 0 through the seam.
    for (int i = 0; i < 600; ++i) {
        const double t = i / 599.0;
        relax(c, /*gamma=*/2.0 * (1.0 - t), /*dtau=*/0.05, /*iterations=*/1);
    }

    // chi = 1 never truncates -> 0 telemetry through the seam.
    EXPECT_EQ(truncation_error(c), 0.0);

    // decisions() is bulk-consistent with per-agent decision_z, all in [-1, 1].
    const std::vector<double> z = decisions(c);
    ASSERT_EQ(z.size(), 3u);
    for (uint32_t i = 0; i < 3; ++i) {
        EXPECT_GE(z[i], -1.0);
        EXPECT_LE(z[i], 1.0);
        EXPECT_DOUBLE_EQ(z[i], decision_z(c, i));
        EXPECT_GT(z[i], 0.8) << "loopy agent " << i << " did not commit +z";
    }

    // Collapse conditions the neighbors -- but only while gamma is still up (with
    // gamma == 0 the transverse field is off, so a saturated register cannot flip;
    // this mirrors loopy_bp_tests' collapse test, which collapses PARTWAY through
    // the anneal). Build a fresh chain, anneal partway, collapse agent 0 to |1>
    // (z = -1) through the seam, then finish: decision_z reflects the pin and the
    // ferro coupling drags the neighbor negative.
    LoopyBpGroup g2 = make_loopy_bp_group(
        3, { ExactBond{ 0, 1, +1.0 }, ExactBond{ 1, 2, +1.0 } });
    Coordination c2 = std::move(g2);
    for (int i = 0; i < 200; ++i) {  // partway: gamma 2 -> 0.5
        relax(c2, /*gamma=*/2.0 - 1.5 * (i / 199.0), /*dtau=*/0.05, /*iterations=*/1);
    }
    collapse(c2, 0, /*bit=*/true);
    for (int i = 0; i < 400; ++i) {  // finish: gamma 0.5 -> 0
        relax(c2, /*gamma=*/0.5 * (1.0 - i / 399.0), /*dtau=*/0.05, /*iterations=*/1);
    }
    EXPECT_LT(decision_z(c2, 0), -0.99) << "collapse not reflected in decision_z";
    EXPECT_LT(decision_z(c2, 1), -0.5) << "neighbor not conditioned on the collapse";
    EXPECT_EQ(truncation_error(c2), 0.0);  // still no truncation after collapse
}

// The general-graph TN backend drives through the same contract -- on a CYCLIC
// topology, which is the whole point of wiring it in: before this, chi >= 2 meant
// the TTN chain and nothing else, so a ring or a star got NO entanglement at any
// cost (chi = 1 took any topology but as a product state).
TEST(Coordination, GraphTnBackendThroughTheContractOnARing)
{
    // A 5-ring -- odd, so an antiferromagnet on it would be frustrated, and in
    // any case not a chain: build_ttn rejects this shape outright.
    std::vector<ExactBond> bonds;
    for (uint32_t i = 0; i < 5; ++i) {
        bonds.push_back(ExactBond{ i, (i + 1u) % 5u, 1.0 });   // ferromagnetic
    }
    GraphTn g = make_graph_tn(5, bonds, /*chi=*/2);
    set_goals(g, { Goal{ .agent = 0, .field = 0.6 } });
    Coordination c = std::move(g);

    relax(c, /*gamma=*/0.1, /*dtau=*/0.05, /*iterations=*/300);

    // The goal propagates all the way round the ring through the couplings.
    for (uint32_t i = 0; i < 5; ++i) {
        EXPECT_GT(decision_z(c, i), 0.5) << "agent " << i;
    }
    // The bulk read agrees with the per-agent one.
    const std::vector<double> z = decisions(c);
    ASSERT_EQ(z.size(), 5u);
    for (uint32_t i = 0; i < 5; ++i) {
        EXPECT_NEAR(z[i], decision_z(c, i), 1e-9);
    }
    // And it reports truncation telemetry through the seam like the TTN does.
    EXPECT_GE(truncation_error(c), 0.0);
}

// A collapsed decision is a HELD CONSTRAINT on EVERY backend, not a one-shot
// projection. This is the contract #298 was filed against: loopy_bp clamped and
// held, while exact and TTN projected once and let the very next relaxation mix
// the latch back out -- so the same authored mind treated a committed order as a
// hard constraint on one backend and a soft nudge on another. Measured with the
// clamp removed, one think of relaxation takes a decision committed to |1> from
// -1.0 to -0.84, and sustained relaxation to +0.72 -- it does not merely drift,
// it FLIPS, so the agent un-decides and commits the opposite while committed()
// still reports the original bit.
TEST(Coordination, ACollapsedDecisionIsHeldThroughRelaxationOnEveryBackend)
{
    // Each case pairs agent 0 with a partner, ferromagnetically, and puts a goal
    // on agent 0 pulling the OTHER way -- so both the coupling and the field
    // actively fight the latch.
    const auto check = [](const char* name, Coordination c) {
        collapse(c, 0, /*bit=*/true);            // |1> -> z = -1
        ASSERT_LT(decision_z(c, 0), -0.9) << name << ": collapse did not take";

        relax(c, /*gamma=*/1.0, /*dtau=*/0.05, /*iterations=*/5);   // one think
        EXPECT_LT(decision_z(c, 0), -0.9) << name << ": drifted off after one think";

        relax(c, 1.0, 0.05, 200);                // many ticks worth
        EXPECT_LT(decision_z(c, 0), -0.9) << name << ": drifted off over time";
        // And the partner is genuinely conditioned by the held decision rather
        // than just following its own goal.
        EXPECT_LT(decision_z(c, 1), 0.0) << name << ": partner not conditioned";
    };

    {
        ExactGroup g = make_exact_group(2, { ExactBond{ 0, 1, 1.0 } });
        set_goals(g, { Goal{ .agent = 0, .field = 0.8 } });   // fights the latch
        check("exact", Coordination{ std::move(g) });
    }
    {
        TtnChain t = make_ttn_chain(2, { 1.0 }, /*chi=*/2);
        t.goal_field[0] = 0.8;
        check("ttn", Coordination{ std::move(t) });
    }
    {
        LoopyBpGroup g = make_loopy_bp_group(2, { ExactBond{ 0, 1, 1.0 } });
        set_goals(g, { Goal{ .agent = 0, .field = 0.8 } });
        check("loopy", Coordination{ std::move(g) });
    }
    {
        // A 4-RING, so this also covers the cyclic topology no other chi >= 2
        // backend can represent.
        std::vector<ExactBond> bonds;
        for (uint32_t i = 0; i < 4; ++i) {
            bonds.push_back(ExactBond{ i, (i + 1u) % 4u, 1.0 });
        }
        GraphTn g = make_graph_tn(4, bonds, /*chi=*/2);
        set_goals(g, { Goal{ .agent = 0, .field = 0.8 } });
        check("graph_tn", Coordination{ std::move(g) });
    }
}
