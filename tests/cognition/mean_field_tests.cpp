#include <engine/cognition/mean_field.h>

#include <graph/shared_edge_polytree.h>
#include <engine/qstate/qstate.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

using namespace wz::cognition;
using wz::core::graph::add_edge;
using wz::core::graph::add_node;
using wz::core::graph::build;
using wz::core::graph::node_data;
using wz::core::graph::SharedEdgePolytreeBuilder;

namespace
{
    // Two single-qubit agents, parent 0 and child 1, coupled with strength j,
    // both starting in equal superposition. The child node is then nudged with a
    // tiny +z bias so the symmetric fixed point is broken (real agents are never
    // perfectly symmetric).
    std::optional<MeanFieldNetwork> make_pair(double j)
    {
        SharedEdgePolytreeBuilder<Node, MeanFieldBond> b;
        add_node(b, wz::qstate::uniform(1));
        add_node(b, wz::qstate::uniform(1));
        if (!add_edge(b, 0u, 1u,
                MeanFieldBond{ .child_qubit = 0, .parent_qubit = 0, .j = j })) {
            return std::nullopt;
        }
        return build(std::move(b));
    }

    double zof(MeanFieldNetwork& net, uint32_t node)
    {
        return wz::qstate::expectation_z(node_data(net, node), 0u);
    }

    void seed_plus_z(MeanFieldNetwork& net, uint32_t node)
    {
        // One weak imaginary-time nudge toward +z -- a small symmetry-breaking bias.
        wz::qstate::apply_imag_time_field(
            node_data(net, node), 0u, /*gamma=*/0.0, /*h=*/0.5, /*dtau=*/0.05);
    }
}

// Ferromagnetic coupling with a weak transverse field: the two agents order and
// ALIGN -- both <sigma_z> saturate to the same (seeded) sign.
TEST(MeanField, FerromagneticCouplingAligns)
{
    auto net = make_pair(/*j=*/0.6);
    ASSERT_TRUE(net.has_value());
    seed_plus_z(*net, 1);

    relax(*net, /*gamma=*/0.05, /*dtau=*/0.05, /*iterations=*/400);

    EXPECT_GT(zof(*net, 0), 0.9);
    EXPECT_GT(zof(*net, 1), 0.9);
}

// Antiferromagnetic coupling: the agents order but ANTI-align -- opposite signs.
TEST(MeanField, AntiferromagneticCouplingAntiAligns)
{
    auto net = make_pair(/*j=*/-0.6);
    ASSERT_TRUE(net.has_value());
    seed_plus_z(*net, 1);

    relax(*net, /*gamma=*/0.05, /*dtau=*/0.05, /*iterations=*/400);

    const double z0 = zof(*net, 0);
    const double z1 = zof(*net, 1);
    EXPECT_LT(z0 * z1, 0.0);                       // opposite signs
    EXPECT_GT(std::max(std::abs(z0), std::abs(z1)), 0.9);  // ordered, not disordered
}

// Strong transverse field dominates the coupling: the agents disorder
// (paramagnet) -- both <sigma_z> relax toward zero regardless of the seed.
TEST(MeanField, StrongTransverseFieldDisorders)
{
    auto net = make_pair(/*j=*/0.6);
    ASSERT_TRUE(net.has_value());
    seed_plus_z(*net, 1);

    relax(*net, /*gamma=*/2.0, /*dtau=*/0.05, /*iterations=*/400);

    EXPECT_LT(std::abs(zof(*net, 0)), 0.3);
    EXPECT_LT(std::abs(zof(*net, 1)), 0.3);
}

// A three-node chain (0-1-2), ferromagnetic: order propagates through the middle
// node so all three align.
TEST(MeanField, OrderPropagatesAlongAChain)
{
    SharedEdgePolytreeBuilder<Node, MeanFieldBond> b;
    add_node(b, wz::qstate::uniform(1));
    add_node(b, wz::qstate::uniform(1));
    add_node(b, wz::qstate::uniform(1));
    ASSERT_TRUE(add_edge(b, 0u, 1u, MeanFieldBond{ .j = 0.6 }));
    ASSERT_TRUE(add_edge(b, 1u, 2u, MeanFieldBond{ .j = 0.6 }));
    auto net = build(std::move(b));
    ASSERT_TRUE(net.has_value());
    seed_plus_z(*net, 0);

    relax(*net, 0.05, 0.05, 400);

    EXPECT_GT(zof(*net, 0), 0.9);
    EXPECT_GT(zof(*net, 1), 0.9);
    EXPECT_GT(zof(*net, 2), 0.9);
}
