#include <engine/cognition/tree_bp.h>

#include <graph/shared_edge_polytree.h>
#include <engine/qstate/qstate.h>

#include <gtest/gtest.h>

#include <algorithm>
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
    // Statevector of a two-site MPS with trivial boundaries (left.left == 1,
    // right.right == 1): psi[s0*2 + s1] = sum_m A[s0][0][m] B[s1][m][0].
    std::vector<Complex> two_site_statevector(const MpsSite& A, const MpsSite& B)
    {
        std::vector<Complex> psi(4, Complex{ 0, 0 });
        for (uint32_t s0 = 0; s0 < 2; ++s0) {
            for (uint32_t s1 = 0; s1 < 2; ++s1) {
                Complex acc{ 0, 0 };
                for (uint32_t m = 0; m < A.right; ++m) {
                    acc += A.a[(s0 * A.left + 0) * A.right + m]
                        * B.a[(s1 * B.left + m) * B.right + 0];
                }
                psi[s0 * 2 + s1] = acc;
            }
        }
        return psi;
    }

    std::vector<Complex> apply_gate_dense(
        const std::vector<Complex>& g, const std::vector<Complex>& psi)
    {
        std::vector<Complex> out(4, Complex{ 0, 0 });
        for (uint32_t o = 0; o < 4; ++o) {
            for (uint32_t i = 0; i < 4; ++i) {
                out[o] += g[o * 4 + i] * psi[i];
            }
        }
        return out;
    }

    double vmax_diff(const std::vector<Complex>& a, const std::vector<Complex>& b)
    {
        double m = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            m = std::max(m, std::abs(a[i] - b[i]));
        }
        return m;
    }

    MpsSite qubit_site(Complex c0, Complex c1)
    {
        MpsSite s;
        s.left = 1;
        s.right = 1;
        s.a = { c0, c1 };  // |c0,c1> (a[s=0]=c0, a[s=1]=c1)
        return s;
    }

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

// With chi large enough that no truncation happens, the two-site update
// reproduces applying the gate to the dense two-site state exactly.
TEST(TwoSite, UntruncatedUpdateMatchesDenseGate)
{
    Rng rng{ 0x2517Eu };
    for (int trial = 0; trial < 10; ++trial) {
        MpsSite A;
        A.left = 1;
        A.right = 2;
        A.a.resize(4);
        MpsSite B;
        B.left = 2;
        B.right = 1;
        B.a.resize(4);
        for (Complex& c : A.a) {
            c = Complex{ rng.next_unit() * 2 - 1, rng.next_unit() * 2 - 1 };
        }
        for (Complex& c : B.a) {
            c = Complex{ rng.next_unit() * 2 - 1, rng.next_unit() * 2 - 1 };
        }
        std::vector<Complex> gate(16);
        for (Complex& c : gate) {
            c = Complex{ rng.next_unit() * 2 - 1, rng.next_unit() * 2 - 1 };
        }

        const auto reference = apply_gate_dense(gate, two_site_statevector(A, B));
        apply_two_site_gate(A, B, gate, /*chi=*/2);  // chi=2 -> no truncation
        EXPECT_LT(vmax_diff(two_site_statevector(A, B), reference), 1e-9)
            << "trial " << trial;
    }
}

// An entangling gate grows the bond and builds the cat state from a product:
// (CNOT)(H x I) |00> = (|00> + |11>)/sqrt2.
TEST(TwoSite, EntanglingGateBuildsCatAndGrowsBond)
{
    const double r = 1.0 / std::sqrt(2.0);
    // G[out*4 + in], out/in = s0*2 + s1.
    const std::vector<Complex> bell = {
        Complex{ r, 0 }, Complex{ 0, 0 }, Complex{ r, 0 }, Complex{ 0, 0 },
        Complex{ 0, 0 }, Complex{ r, 0 }, Complex{ 0, 0 }, Complex{ r, 0 },
        Complex{ 0, 0 }, Complex{ r, 0 }, Complex{ 0, 0 }, Complex{ -r, 0 },
        Complex{ r, 0 }, Complex{ 0, 0 }, Complex{ -r, 0 }, Complex{ 0, 0 },
    };
    MpsSite A = qubit_site(Complex{ 1, 0 }, Complex{ 0, 0 });  // |0>
    MpsSite B = qubit_site(Complex{ 1, 0 }, Complex{ 0, 0 });  // |0>

    apply_two_site_gate(A, B, bell, /*chi=*/2);
    EXPECT_EQ(A.right, 2u);  // product (bond 1) -> entangled (bond 2)
    EXPECT_EQ(B.left, 2u);

    const auto psi = two_site_statevector(A, B);  // expect (|00> + |11>)/sqrt2
    const std::vector<Complex> cat = {
        Complex{ r, 0 }, Complex{ 0, 0 }, Complex{ 0, 0 }, Complex{ r, 0 }
    };
    EXPECT_LT(vmax_diff(psi, cat), 1e-9);
}

// Truncating to chi = 1 collapses an entangled bond back to a product bond.
TEST(TwoSite, TruncationToChiOneCollapsesTheBond)
{
    const double r = 1.0 / std::sqrt(2.0);
    const std::vector<Complex> bell = {
        Complex{ r, 0 }, Complex{ 0, 0 }, Complex{ r, 0 }, Complex{ 0, 0 },
        Complex{ 0, 0 }, Complex{ r, 0 }, Complex{ 0, 0 }, Complex{ r, 0 },
        Complex{ 0, 0 }, Complex{ r, 0 }, Complex{ 0, 0 }, Complex{ -r, 0 },
        Complex{ r, 0 }, Complex{ 0, 0 }, Complex{ -r, 0 }, Complex{ 0, 0 },
    };
    MpsSite A = qubit_site(Complex{ 1, 0 }, Complex{ 0, 0 });
    MpsSite B = qubit_site(Complex{ 1, 0 }, Complex{ 0, 0 });
    apply_two_site_gate(A, B, bell, 2);  // make the cat (bond 2)
    ASSERT_EQ(A.right, 2u);

    std::vector<Complex> identity(16, Complex{ 0, 0 });
    for (uint32_t i = 0; i < 4; ++i) {
        identity[i * 4 + i] = Complex{ 1, 0 };
    }
    apply_two_site_gate(A, B, identity, /*chi=*/1);  // truncate the bond
    EXPECT_EQ(A.right, 1u);
    EXPECT_EQ(B.left, 1u);
}
