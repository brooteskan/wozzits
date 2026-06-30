#include <engine/cognition/coordination.h>

#include <graph/shared_edge_polytree.h>
#include <engine/qstate/qstate.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <utility>

using namespace wz::cognition;

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

// The mean-field backend drives through the same contract.
TEST(Coordination, MeanFieldBackendThroughTheContract)
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
    wz::qstate::apply_imag_time_field(node_data(*net, 1), 0u, 0.0, 0.5, 0.05);

    Coordination c = std::move(*net);
    relax(c, 0.05, 0.05, 400);
    EXPECT_GT(decision_z(c, 0) * decision_z(c, 1), 0.0);          // aligned
    EXPECT_GT(std::min(std::abs(decision_z(c, 0)),
                  std::abs(decision_z(c, 1))),
        0.5);  // committed
}
