#include <engine/cognition/anneal.h>

#include <graph/shared_edge_polytree.h>
#include <engine/qstate/qstate.h>

#include <gtest/gtest.h>

#include <cmath>
#include <utility>

using namespace wz::cognition;

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
    add_node(b, wz::qstate::uniform(1));
    add_node(b, wz::qstate::uniform(1));
    ASSERT_TRUE(add_edge(b, 0u, 1u, MeanFieldBond{ .j = 0.6 }));
    auto net = build(std::move(b));
    ASSERT_TRUE(net.has_value());

    // Tiny symmetry-breaking +z seed on one agent.
    wz::qstate::apply_imag_time_field(node_data(*net, 1), 0u, 0.0, 0.5, 0.05);

    anneal(*net, AnnealSchedule{ .gamma_start = 0.8, .gamma_end = 0.02,
        .dtau = 0.05, .steps = 400 });

    const double z0 = decision_z(*net, 0);
    const double z1 = decision_z(*net, 1);
    EXPECT_GT(z0 * z1, 0.0);                       // aligned
    EXPECT_GT(std::min(std::abs(z0), std::abs(z1)), 0.5);  // committed, not undecided
}
