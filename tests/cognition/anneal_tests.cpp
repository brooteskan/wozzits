#include <cognition/anneal.h>

#include <cognition/graph_tn.h>
#include <cognition/loopy_bp.h>
#include <cognition/ttn.h>
#include <graph/shared_edge_polytree.h>
#include <cognition/qstate/qstate.h>

#include <gtest/gtest.h>

#include <cmath>
#include <utility>
#include <vector>

using namespace wz::engine::cognition;

TEST(Anneal, GammaRampIsLinear)
{
    AnnealSchedule s{ .gamma_start = 2.0, .gamma_end = 0.0,
        .dtau = 0.05, .steps = 11 };
    EXPECT_NEAR(gamma_at(s, 0), 2.0, 1e-12);
    EXPECT_NEAR(gamma_at(s, 5), 1.0, 1e-12);
    EXPECT_NEAR(gamma_at(s, 10), 0.0, 1e-12);
}

// Ramping Gamma DOWN drives a ferromagnetic group from undecided to a committed
// joint decision: the connected correlation rises to ~+1 (the entangled cat).
TEST(Anneal, RampingGammaDownCommitsAGroupDecision)
{
    ExactGroup g = make_exact_group(2, { ExactBond{ 0, 1, 1.0 } });
    anneal(g, AnnealSchedule{ .gamma_start = 3.0, .gamma_end = 0.05,
        .dtau = 0.05, .steps = 400 });
    EXPECT_GT(connected_correlation(g, 0, 1), 0.95);
}

// Holding Gamma HIGH (a flat schedule that never ramps down) leaves the group
// exploratory and undecided -- it is the ramp that commits, not just running.
TEST(Anneal, HoldingGammaHighLeavesGroupUndecided)
{
    ExactGroup g = make_exact_group(2, { ExactBond{ 0, 1, 1.0 } });
    anneal(g, AnnealSchedule{ .gamma_start = 6.0, .gamma_end = 6.0,
        .dtau = 0.05, .steps = 400 });
    EXPECT_LT(std::abs(connected_correlation(g, 0, 1)), 0.15);
}

// The schedule drives the mean-field backend too: a seeded ferromagnetic pair
// anneals to an aligned committed decision.
TEST(Anneal, MeanFieldGroupCommitsUnderAnnealing)
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

    // Tiny symmetry-breaking +z seed on one agent.
    wz::engine::cognition::qstate::apply_imag_time_field(node_data(*net, 1), 0u, 0.0, 0.5, 0.05);

    anneal(*net, AnnealSchedule{ .gamma_start = 0.8, .gamma_end = 0.02,
        .dtau = 0.05, .steps = 400 });

    const double z0 = decision_z(*net, 0);
    const double z1 = decision_z(*net, 1);
    EXPECT_GT(z0 * z1, 0.0);                       // aligned
    EXPECT_GT(std::min(std::abs(z0), std::abs(z1)), 0.5);  // committed, not undecided
}

// THE unification test. There is one Gamma ramp (cognition_clock's
// gamma_at_phase) and two drivers over it: the SIM-TIME one the engine runs
// (gamma_at_time) and the STEP-COUNT one tests use (gamma_at). At equal phase
// they must agree EXACTLY -- not approximately -- or the schedule the paper
// describes is not the schedule the engine runs.
//
// Each driver used to carry its own copy of the interpolation (and a third lived
// in loopy_bp_tests), so nothing stopped them drifting apart.
TEST(Anneal, StepCountAndSimTimeDriversAgreeAtEqualPhase)
{
    constexpr double kStart = 3.0;
    constexpr double kEnd = 0.25;

    CognitionClock clock;
    clock.gamma_start = kStart;
    clock.gamma_end = kEnd;
    clock.anneal_seconds = 4.0;
    start(clock, /*now=*/10.0);   // a non-zero origin, so phase != absolute time

    constexpr uint32_t kSteps = 9;
    AnnealSchedule s{ .gamma_start = kStart, .gamma_end = kEnd,
        .dtau = 0.05, .steps = kSteps };

    for (uint32_t i = 0; i < kSteps; ++i) {
        const double phase =
            static_cast<double>(i) / static_cast<double>(kSteps - 1);
        const double sim_time = clock.started_at + phase * clock.anneal_seconds;
        EXPECT_DOUBLE_EQ(gamma_at(s, i), gamma_at_time(clock, sim_time))
            << "step " << i << " (phase " << phase << ")";
    }

    // Both clamp outside the sweep rather than extrapolating.
    EXPECT_DOUBLE_EQ(gamma_at_time(clock, clock.started_at - 100.0), kStart);
    EXPECT_DOUBLE_EQ(gamma_at_time(clock, clock.started_at + 100.0), kEnd);
    EXPECT_DOUBLE_EQ(gamma_at_phase(kStart, kEnd, -5.0), kStart);
    EXPECT_DOUBLE_EQ(gamma_at_phase(kStart, kEnd, 5.0), kEnd);
}

// The step-count driver now takes ANY backend exposing relax_step, not the two
// it had hand-written overloads for. That is what let loopy_bp_tests drop its
// private copy of the ramp -- LoopyBpGroup was exactly the backend the old
// overload set could not drive.
TEST(Anneal, DrivesBackendsBeyondTheOldTwoOverloads)
{
    const AnnealSchedule s{ .gamma_start = 2.0, .gamma_end = 0.05,
        .dtau = 0.05, .steps = 300 };

    // chi = 1, cyclic: previously needed a hand-rolled ramp in the test file.
    {
        LoopyBpGroup g = make_loopy_bp_group(
            3, { ExactBond{ 0, 1, 1.0 }, ExactBond{ 1, 2, 1.0 } });
        set_goals(g, { Goal{ .agent = 0, .field = 0.6 } });
        anneal(g, s);
        EXPECT_GT(decision_z(g, 0) * decision_z(g, 2), 0.0);   // ferro: aligned
    }
    // chi >= 2 chain.
    {
        TtnChain t = make_ttn_chain(3, { 1.0, 1.0 }, /*chi=*/2);
        t.goal_field[0] = 0.6;
        anneal(t, s);
        const std::vector<double> z = decisions(t);
        EXPECT_GT(z[0] * z[2], 0.0);
    }
    // chi >= 2 on a RING -- the general graph backend, which did not exist in
    // the Coordination seam when anneal() was written.
    {
        GraphTn g = make_graph_tn(
            3, { ExactBond{ 0, 1, 1.0 }, ExactBond{ 1, 2, 1.0 },
                ExactBond{ 2, 0, 1.0 } }, /*chi=*/2);
        set_goals(g, { Goal{ .agent = 0, .field = 0.6 } });
        anneal(g, s);
        EXPECT_GT(decision_z(g, 0) * decision_z(g, 2), 0.0);
    }
}
