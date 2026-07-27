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

// The mean-field backend drives through the same contract.
TEST(Coordination, MeanFieldBackendThroughTheContract)
{
    using wz::core::graph::add_edge;
    using wz::core::graph::add_node;
    using wz::core::graph::build;
    using wz::core::graph::node_data;
    using wz::core::graph::SharedEdgePolytreeBuilder;

    SharedEdgePolytreeBuilder<Node, MeanFieldBond> b;
    add_node(b, wz::engine::cognition::qstate::uniform(1));
    add_node(b, wz::engine::cognition::qstate::uniform(1));
    ASSERT_TRUE(add_edge(b, 0u, 1u, MeanFieldBond{ .j = 0.6 }));
    auto net = build(std::move(b));
    ASSERT_TRUE(net.has_value());
    wz::engine::cognition::qstate::apply_imag_time_field(node_data(*net, 1), 0u, 0.0, 0.5, 0.05);

    Coordination c = std::move(*net);
    relax(c, 0.05, 0.05, 400);
    EXPECT_GT(decision_z(c, 0) * decision_z(c, 1), 0.0);          // aligned
    EXPECT_GT(std::min(std::abs(decision_z(c, 0)),
                  std::abs(decision_z(c, 1))),
        0.5);  // committed
}

// A collapsed decision is a HELD CONSTRAINT on every backend, not a one-shot
// projection. This is the contract #298 was filed against: loopy_bp clamped and
// held, while exact and TTN projected once and let the very next relaxation mix
// the latch back out -- so the same authored mind treated a committed order as a
// hard constraint on one backend and an ~8%-per-tick nudge on another. Measured
// before the fix, one think's worth of relaxation (5 substeps, dtau 0.05) after
// collapsing agent 0 to |1>:
//
//     exact:  -1.0000 -> -0.9224   (drifted off the latch)
//     loopy:  -1.0000 -> -1.0000   (clamped, held)
//
// Every backend must now read like the loopy row -- and hold it over a LONG
// relaxation, not just one tick, because the drift compounds.
TEST(Coordination, ACollapsedDecisionIsHeldThroughRelaxationOnEveryBackend)
{
    // A ferromagnetic pair with a goal pulling agent 0 the OTHER way, so both the
    // coupling and the field actively fight the latch.
    const auto check = [](const char* name, Coordination c) {
        collapse(c, 0, /*bit=*/true);            // |1> -> z = -1
        ASSERT_LT(decision_z(c, 0), -0.99) << name << ": collapse did not take";

        relax(c, /*gamma=*/1.0, /*dtau=*/0.05, /*iterations=*/5);   // one think
        EXPECT_LT(decision_z(c, 0), -0.99) << name << ": drifted off after one think";

        relax(c, 1.0, 0.05, 200);                // many ticks' worth
        EXPECT_LT(decision_z(c, 0), -0.99) << name << ": drifted off over time";
        // And the partner is genuinely conditioned by the held decision, not just
        // following its own goal.
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
}
