#include <cognition/tree_tn.h>

#include <cognition/tree_bp.h>
#include <graph/shared_edge_polytree.h>
#include <cognition/qstate/qstate.h>

#include <gtest/gtest.h>

#include <utility>
#include <vector>

using namespace wz::engine::cognition;
using wz::core::graph::add_edge;
using wz::core::graph::add_node;
using wz::core::graph::build;
using wz::core::graph::NodeHandle;
using wz::core::graph::SharedEdgePolytreeBuilder;
using wz::engine::cognition::qstate::Complex;
using wz::engine::cognition::qstate::Rng;

namespace
{
    TreeNode tn_node(uint32_t parent_bond, std::vector<uint32_t> child_bonds,
        std::vector<Complex> t)
    {
        TreeNode n;
        n.parent_bond = parent_bond;
        n.child_bonds = std::move(child_bonds);
        n.t = std::move(t);
        return n;
    }
}

// A chain expressed with general TreeNodes (each <=1 child) reproduces the MPS
// contractor's marginals on the same random tensors -- the general contraction
// reduces correctly to the chain case.
TEST(TreeTn, ChainMatchesMpsContraction)
{
    Rng rng{ 0x7AEEu };
    for (int trial = 0; trial < 15; ++trial) {
        const uint32_t n = 4;
        const uint32_t chi = 2;

        SharedEdgePolytreeBuilder<MpsSite, BondEnv> mb;
        SharedEdgePolytreeBuilder<TreeNode, BondEnv> tb;
        std::vector<NodeHandle> mnodes;
        std::vector<NodeHandle> tnodes;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t L = (i == 0) ? 1u : chi;
            const uint32_t R = (i == n - 1) ? 1u : chi;
            std::vector<Complex> data(static_cast<std::size_t>(2) * L * R);
            for (Complex& c : data) {
                c = Complex{ rng.next_unit() * 2 - 1, rng.next_unit() * 2 - 1 };
            }
            MpsSite ms;
            ms.left = L;
            ms.right = R;
            ms.a = data;
            mnodes.push_back(add_node(mb, std::move(ms)));

            // Same flat data; a chain node has one child bond (R) unless it is the
            // last site (a leaf: no children, R == 1 folds away).
            std::vector<uint32_t> children;
            if (i != n - 1) {
                children = { R };
            }
            tnodes.push_back(add_node(tb, tn_node(L, children, std::move(data))));
        }
        for (uint32_t i = 1; i < n; ++i) {
            add_edge(mb, mnodes[i - 1], mnodes[i], BondEnv{});
            add_edge(tb, tnodes[i - 1], tnodes[i], BondEnv{});
        }

        TreeBpNetwork mps = std::move(*build(std::move(mb)));
        TreeTnNetwork tn = std::move(*build(std::move(tb)));

        const auto ref = tree_bp_sigma_z(mps);
        const auto got = tree_tn_sigma_z(tn);
        ASSERT_EQ(ref.size(), got.size());
        for (std::size_t i = 0; i < ref.size(); ++i) {
            EXPECT_NEAR(got[i], ref[i], 1e-9) << "trial " << trial << " site " << i;
        }
    }
}

// A STAR (a centre node with two leaf children) in the product |0> state:
// every agent's <sigma_z> is +1. This exercises the branching contraction.
TEST(TreeTn, ProductStarAllUp)
{
    SharedEdgePolytreeBuilder<TreeNode, BondEnv> b;
    // centre (root): two child bonds of dim 1; |0>.
    const NodeHandle c =
        add_node(b, tn_node(1, { 1, 1 }, { Complex{ 1, 0 }, Complex{ 0, 0 } }));
    const NodeHandle l1 =
        add_node(b, tn_node(1, {}, { Complex{ 1, 0 }, Complex{ 0, 0 } }));
    const NodeHandle l2 =
        add_node(b, tn_node(1, {}, { Complex{ 1, 0 }, Complex{ 0, 0 } }));
    add_edge(b, c, l1, BondEnv{});
    add_edge(b, c, l2, BondEnv{});
    TreeTnNetwork net = std::move(*build(std::move(b)));

    const auto z = tree_tn_sigma_z(net);
    ASSERT_EQ(z.size(), 3u);
    for (double v : z) {
        EXPECT_NEAR(v, 1.0, 1e-12);
    }
}

// A GHZ star: the centre's physical qubit is tied to both child bonds, and each
// leaf ties its qubit to its bond, giving (|000> + |111>)/sqrt2 -- every agent is
// unpolarized (<sigma_z> = 0). The entangled branching case, with a known answer.
TEST(TreeTn, GhzStarIsUnpolarized)
{
    // centre: parent_bond 1, child_bonds {2,2}; t[(s*2+b1)*2+b2] = [s==b1==b2].
    std::vector<Complex> ct(8, Complex{ 0, 0 });
    ct[(0 * 2 + 0) * 2 + 0] = Complex{ 1, 0 };  // s=0,b1=0,b2=0
    ct[(1 * 2 + 1) * 2 + 1] = Complex{ 1, 0 };  // s=1,b1=1,b2=1
    // leaf: parent_bond 2; t[s*2 + a] = [s==a].
    const std::vector<Complex> lt = {
        Complex{ 1, 0 }, Complex{ 0, 0 }, Complex{ 0, 0 }, Complex{ 1, 0 }
    };

    SharedEdgePolytreeBuilder<TreeNode, BondEnv> b;
    const NodeHandle c = add_node(b, tn_node(1, { 2, 2 }, ct));
    const NodeHandle l1 = add_node(b, tn_node(2, {}, lt));
    const NodeHandle l2 = add_node(b, tn_node(2, {}, lt));
    add_edge(b, c, l1, BondEnv{});
    add_edge(b, c, l2, BondEnv{});
    TreeTnNetwork net = std::move(*build(std::move(b)));

    const auto z = tree_tn_sigma_z(net);
    ASSERT_EQ(z.size(), 3u);
    for (double v : z) {
        EXPECT_NEAR(v, 0.0, 1e-12);
    }
}
