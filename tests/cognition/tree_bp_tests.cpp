#include <engine/cognition/tree_bp.h>

#include <graph/shared_edge_polytree.h>
#include <engine/qstate/qstate.h>

#include <gtest/gtest.h>

#include <utility>
#include <vector>

using namespace wz::cognition;
using wz::core::graph::add_edge;
using wz::core::graph::add_node;
using wz::core::graph::build;
using wz::core::graph::NodeHandle;
using wz::core::graph::SharedEdgePolytreeBuilder;
using wz::qstate::Complex;
using wz::qstate::Rng;

namespace
{
    // A chain MPS of `n` single-qubit sites with bulk bond dimension `chi`, filled
    // with random tensor entries.
    TreeBpNetwork build_random_chain(uint32_t n, uint32_t chi, Rng& rng)
    {
        SharedEdgePolytreeBuilder<MpsSite, BondEnv> b;
        std::vector<NodeHandle> nodes;
        for (uint32_t i = 0; i < n; ++i) {
            MpsSite s;
            s.left = (i == 0) ? 1u : chi;
            s.right = (i == n - 1) ? 1u : chi;
            s.a.resize(static_cast<std::size_t>(2) * s.left * s.right);
            for (Complex& c : s.a) {
                c = Complex{ rng.next_unit() * 2.0 - 1.0,
                    rng.next_unit() * 2.0 - 1.0 };
            }
            nodes.push_back(add_node(b, std::move(s)));
        }
        for (uint32_t i = 1; i < n; ++i) {
            add_edge(b, nodes[i - 1], nodes[i], BondEnv{});
        }
        return std::move(*build(std::move(b)));
    }
}

// Exact tree BP reproduces the dense contraction's single-site marginals for
// random MPS chains -- BP is exact on a tree.
TEST(TreeBP, MatchesDenseContractionForRandomChains)
{
    Rng rng{ 0xA11CEu };
    for (int trial = 0; trial < 20; ++trial) {
        TreeBpNetwork net = build_random_chain(/*n=*/4, /*chi=*/2, rng);
        const auto bp = tree_bp_sigma_z(net);
        const auto dense = dense_sigma_z(net);
        ASSERT_EQ(bp.size(), dense.size());
        for (std::size_t i = 0; i < bp.size(); ++i) {
            EXPECT_NEAR(bp[i], dense[i], 1e-9) << "trial " << trial << " site " << i;
        }
    }
}

TEST(TreeBP, MatchesDenseAtChiThree)
{
    Rng rng{ 0xBEE5u };
    TreeBpNetwork net = build_random_chain(/*n=*/3, /*chi=*/3, rng);
    const auto bp = tree_bp_sigma_z(net);
    const auto dense = dense_sigma_z(net);
    for (std::size_t i = 0; i < bp.size(); ++i) {
        EXPECT_NEAR(bp[i], dense[i], 1e-9);
    }
}

// A product chain of |0> sites: every <sigma_z> is +1, by both methods.
TEST(TreeBP, ProductStateOfZeros)
{
    SharedEdgePolytreeBuilder<MpsSite, BondEnv> b;
    std::vector<NodeHandle> nodes;
    for (uint32_t i = 0; i < 4; ++i) {
        MpsSite s;
        s.left = 1;
        s.right = 1;
        s.a = { Complex{ 1, 0 }, Complex{ 0, 0 } };  // |0>: a[s=0]=1, a[s=1]=0
        nodes.push_back(add_node(b, std::move(s)));
    }
    for (uint32_t i = 1; i < 4; ++i) {
        add_edge(b, nodes[i - 1], nodes[i], BondEnv{});
    }
    TreeBpNetwork net = std::move(*build(std::move(b)));

    const auto bp = tree_bp_sigma_z(net);
    for (double z : bp) {
        EXPECT_NEAR(z, 1.0, 1e-12);
    }
}

// A two-site cat MPS (|00> + |11>)/sqrt2: both sites are unpolarized
// (<sigma_z> = 0), and BP matches the dense result -- a genuinely entangled
// (chi = 2) state contracted exactly.
TEST(TreeBP, EntangledCatChainIsUnpolarized)
{
    SharedEdgePolytreeBuilder<MpsSite, BondEnv> b;
    // Site 0: left 1, right 2.  a[(s*1+0)*2 + r]:  s0 -> r0,  s1 -> r1.
    MpsSite s0;
    s0.left = 1;
    s0.right = 2;
    s0.a = { Complex{ 1, 0 }, Complex{ 0, 0 },    // s=0: r0=1, r1=0
        Complex{ 0, 0 }, Complex{ 1, 0 } };        // s=1: r0=0, r1=1
    // Site 1: left 2, right 1.  a[(s*2+l)*1+0]:  s0 -> l0,  s1 -> l1.
    MpsSite s1;
    s1.left = 2;
    s1.right = 1;
    s1.a = { Complex{ 1, 0 }, Complex{ 0, 0 },    // s=0: l0=1, l1=0
        Complex{ 0, 0 }, Complex{ 1, 0 } };        // s=1: l0=0, l1=1
    const NodeHandle n0 = add_node(b, std::move(s0));
    const NodeHandle n1 = add_node(b, std::move(s1));
    add_edge(b, n0, n1, BondEnv{});
    TreeBpNetwork net = std::move(*build(std::move(b)));

    const auto bp = tree_bp_sigma_z(net);
    const auto dense = dense_sigma_z(net);
    ASSERT_EQ(bp.size(), 2u);
    EXPECT_NEAR(bp[0], 0.0, 1e-12);
    EXPECT_NEAR(bp[1], 0.0, 1e-12);
    EXPECT_NEAR(bp[0], dense[0], 1e-12);
    EXPECT_NEAR(bp[1], dense[1], 1e-12);
}
