#include <cognition/tree_tn.h>

#include <cognition/tree_bp.h>
#include <graph/shared_edge_polytree.h>
#include <cognition/qstate/qstate.h>
#include <tasks/task_scheduler.h>

#include <gtest/gtest.h>

#include <complex>
#include <cstdint>
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
    // Installs a pool for a scope and ALWAYS uninstalls it. The raw
    // set_task_scheduler(&pool) ... set_task_scheduler(nullptr) pairs this
    // replaces sat around ASSERT_EQs: one failing assert returned from the test
    // with the global still pointing at a pool that was about to destruct, so the
    // next test to dispatch a task used a dangling scheduler -- crashing somewhere
    // else and blaming the wrong test.
    struct ScopedPool
    {
        wz::tasks::TaskScheduler pool;

        explicit ScopedPool(unsigned workers) : pool(workers)
        {
            wz::tasks::set_task_scheduler(&pool);
        }
        ~ScopedPool() { wz::tasks::set_task_scheduler(nullptr); }

        ScopedPool(const ScopedPool&) = delete;
        ScopedPool& operator=(const ScopedPool&) = delete;
    };

    TreeNode tn_node(uint32_t parent_bond, std::vector<uint32_t> child_bonds,
        std::vector<Complex> t)
    {
        TreeNode n;
        n.parent_bond = parent_bond;
        n.child_bonds = std::move(child_bonds);
        n.t = std::move(t);
        return n;
    }

    // ─── Independent brute-force reference ────────────────────────────────────
    //
    // Materializes the full 2^N statevector by contracting the ENTIRE tree tensor
    // network (a plain odometer sum over every bond-index assignment), then reads
    // each site's <sigma_z> directly. Deliberately unrelated to the message-passing
    // contraction under test, so it can pin its correctness.
    //
    // Bond convention matches tree_tn: each non-root child c owns ONE bond, of
    // dim node[c].parent_bond, shared with its parent (which lists it in
    // child_bonds in child order). A node's tensor is row-major over
    // [physical(2), parent_bond, child_bonds...].
    std::vector<double> dense_tree_sigma_z(const TreeTnNetwork& net)
    {
        using wz::core::graph::child_count;
        using wz::core::graph::children;
        using wz::core::graph::is_root;
        using wz::core::graph::node_count;
        using wz::core::graph::node_data;

        const uint32_t N = node_count(net);

        // Each non-root node's parent bond is a distinct contraction index. Give
        // every such bond a global id (= the child node's handle) and its dim.
        std::vector<uint32_t> bond_dim(N, 1);  // indexed by child node handle
        std::vector<uint32_t> bond_ids;        // list of bonds to enumerate
        for (NodeHandle n = 0; n < N; ++n) {
            if (!is_root(net, n)) {
                bond_dim[n] = node_data(net, n).parent_bond;
                bond_ids.push_back(n);
            }
        }
        const std::size_t num_bonds = bond_ids.size();

        // For a given physical config (bit s_n per node) and bond assignment
        // (value per bond), the amplitude is the product over nodes of that node's
        // tensor entry, indexing its parent bond by the parent-edge value and each
        // child bond by that child's bond value.
        auto amp_for = [&](uint64_t phys, const std::vector<uint32_t>& bond_val)
            -> Complex {
            Complex a{ 1, 0 };
            for (NodeHandle n = 0; n < N; ++n) {
                const TreeNode& T = node_data(net, n);
                const uint32_t s = static_cast<uint32_t>((phys >> n) & 1u);
                const uint32_t a_bond =
                    is_root(net, n) ? 0u : bond_val[n];  // parent bond value
                std::vector<uint32_t> cb;
                cb.reserve(child_count(net, n));
                for (NodeHandle c : children(net, n)) {
                    cb.push_back(bond_val[c]);  // child c owns bond id c
                }
                // Flat index for [2, parent_bond, child_bonds...].
                std::size_t idx = s;
                idx = idx * T.parent_bond + a_bond;
                for (std::size_t k = 0; k < cb.size(); ++k) {
                    idx = idx * T.child_bonds[k] + cb[k];
                }
                a *= T.t[idx];
            }
            return a;
        };

        // Full statevector amp[phys] = sum over all bond assignments of amp_for.
        const uint64_t phys_dim = (1ull << N);
        std::vector<Complex> psi(phys_dim, Complex{ 0, 0 });
        std::vector<uint32_t> bond_val(N, 0);  // bond value indexed by child handle
        for (uint64_t phys = 0; phys < phys_dim; ++phys) {
            // odometer over the bonds
            for (uint32_t& b : bond_val) {
                b = 0;
            }
            bool more = true;
            while (more) {
                psi[phys] += amp_for(phys, bond_val);
                // advance odometer over bond_ids
                more = false;
                for (std::size_t k = 0; k < num_bonds; ++k) {
                    const NodeHandle id = bond_ids[k];
                    if (++bond_val[id] < bond_dim[id]) {
                        more = true;
                        break;
                    }
                    bond_val[id] = 0;
                }
                if (num_bonds == 0) {
                    break;  // no bonds: exactly one term
                }
            }
        }

        // Per-site <sigma_z> = (sum |amp|^2 * sign) / (sum |amp|^2), sign = +1 if
        // bit == 0, -1 if bit == 1.
        std::vector<double> sz(N, 0.0);
        double total = 0.0;
        for (uint64_t phys = 0; phys < phys_dim; ++phys) {
            total += std::norm(psi[phys]);
        }
        for (NodeHandle n = 0; n < N; ++n) {
            double num = 0.0;
            for (uint64_t phys = 0; phys < phys_dim; ++phys) {
                const double p = std::norm(psi[phys]);
                const double sign = ((phys >> n) & 1u) ? -1.0 : 1.0;
                num += p * sign;
            }
            sz[n] = total > 0 ? num / total : 0.0;
        }
        return sz;
    }

    // Fill a vector with random complex entries in [-1,1] + i[-1,1].
    void fill_random(std::vector<Complex>& v, Rng& rng)
    {
        for (Complex& c : v) {
            c = Complex{ rng.next_unit() * 2 - 1, rng.next_unit() * 2 - 1 };
        }
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

// The branching contraction (the one-at-a-time message folding) must match an
// INDEPENDENT dense reference on random branching trees with entangling centres
// and MIXED child bond dims. Covers a degree-3 star, a degree-4 star, and a
// small 2-level tree. Random complex tensors, unnormalized -- the reference and
// the contractor must agree regardless.
TEST(TreeTn, BranchingMatchesDenseReference)
{
    Rng rng{ 0xC0FFEEu };

    auto tensor_size = [](uint32_t parent_bond,
        const std::vector<uint32_t>& child_bonds) {
        std::size_t sz = static_cast<std::size_t>(2) * parent_bond;
        for (uint32_t d : child_bonds) {
            sz *= d;
        }
        return sz;
    };

    // Build a star: a root centre with `leaves` leaf children, whose bonds have
    // the given (mixed) dims. Each leaf's parent_bond == its shared bond dim, and
    // the leaf's tensor is 2 x dim (random). The centre entangles all children.
    auto build_star = [&](const std::vector<uint32_t>& leaf_dims) {
        SharedEdgePolytreeBuilder<TreeNode, BondEnv> b;
        std::vector<Complex> ct(tensor_size(1, leaf_dims));
        fill_random(ct, rng);
        const NodeHandle centre = add_node(b, tn_node(1, leaf_dims, ct));
        for (uint32_t d : leaf_dims) {
            std::vector<Complex> lt(tensor_size(d, {}));
            fill_random(lt, rng);
            const NodeHandle leaf = add_node(b, tn_node(d, {}, std::move(lt)));
            add_edge(b, centre, leaf, BondEnv{});
        }
        return std::move(*build(std::move(b)));
    };

    // ── degree-3 star, mixed bond dims (2, 3, 2) → 4 qubits ──
    for (int trial = 0; trial < 8; ++trial) {
        TreeTnNetwork net = build_star({ 2, 3, 2 });
        const auto got = tree_tn_sigma_z(net);
        const auto ref = dense_tree_sigma_z(net);
        ASSERT_EQ(got.size(), ref.size());
        for (std::size_t i = 0; i < got.size(); ++i) {
            EXPECT_NEAR(got[i], ref[i], 1e-9)
                << "deg-3 star trial " << trial << " site " << i;
        }
    }

    // ── degree-4 star, mixed bond dims (2, 2, 3, 2) → 5 qubits ──
    for (int trial = 0; trial < 8; ++trial) {
        TreeTnNetwork net = build_star({ 2, 2, 3, 2 });
        const auto got = tree_tn_sigma_z(net);
        const auto ref = dense_tree_sigma_z(net);
        ASSERT_EQ(got.size(), ref.size());
        for (std::size_t i = 0; i < got.size(); ++i) {
            EXPECT_NEAR(got[i], ref[i], 1e-9)
                << "deg-4 star trial " << trial << " site " << i;
        }
    }

    // ── small 2-level tree: root with 2 children, one of which is itself a
    //    branching centre with 2 leaf children. Mixed bond dims. 6 qubits.
    //    Topology (bond dims in parens):
    //        root
    //         ├── A(2)   [leaf]
    //         └── B(3)   [centre] ── C(2) [leaf]
    //                              └─ D(2) [leaf]
    //    → root: child_bonds {2, 3}; B: parent_bond 3, child_bonds {2, 2}.
    for (int trial = 0; trial < 8; ++trial) {
        SharedEdgePolytreeBuilder<TreeNode, BondEnv> b;

        std::vector<Complex> root_t(tensor_size(1, { 2, 3 }));
        fill_random(root_t, rng);
        const NodeHandle root = add_node(b, tn_node(1, { 2, 3 }, root_t));

        std::vector<Complex> a_t(tensor_size(2, {}));
        fill_random(a_t, rng);
        const NodeHandle A = add_node(b, tn_node(2, {}, a_t));

        std::vector<Complex> bcentre_t(tensor_size(3, { 2, 2 }));
        fill_random(bcentre_t, rng);
        const NodeHandle B = add_node(b, tn_node(3, { 2, 2 }, bcentre_t));

        std::vector<Complex> c_t(tensor_size(2, {}));
        fill_random(c_t, rng);
        const NodeHandle C = add_node(b, tn_node(2, {}, c_t));

        std::vector<Complex> d_t(tensor_size(2, {}));
        fill_random(d_t, rng);
        const NodeHandle D = add_node(b, tn_node(2, {}, d_t));

        add_edge(b, root, A, BondEnv{});
        add_edge(b, root, B, BondEnv{});
        add_edge(b, B, C, BondEnv{});
        add_edge(b, B, D, BondEnv{});

        TreeTnNetwork net = std::move(*build(std::move(b)));
        const auto got = tree_tn_sigma_z(net);
        const auto ref = dense_tree_sigma_z(net);
        ASSERT_EQ(got.size(), ref.size());
        for (std::size_t i = 0; i < got.size(); ++i) {
            EXPECT_NEAR(got[i], ref[i], 1e-9)
                << "2-level tree trial " << trial << " site " << i;
        }
    }
}

// S1 (#293): with the wz::tasks worker pool installed, the collect/distribute
// sweeps fan their child subtrees onto worker threads. Because each subtree writes
// only its own messages and the parent folds children in fixed CSR order, the
// result must be BIT-IDENTICAL to the serial contraction -- not merely close. This
// pins that the pool changes only WHEN work runs, never the numbers (and that the
// concurrent subtrees are race-free). A wide star gives the root many independent
// children, so there is real cross-thread fan-out.
TEST(TreeTn, PoolContractionIsBitIdenticalToSerial)
{
    // This file had no force-serial guard, unlike the tasks suite. Without it,
    // WZ_TASKS_FORCE_SERIAL=1 in the environment makes the "pooled" run below
    // execute inline on this thread -- so the test compares serial against serial
    // and passes brilliantly while testing nothing at all. CMake pins the var to 0
    // for the test process; this catches a run where that did not take.
    ASSERT_FALSE(wz::tasks::force_serial())
        << "WZ_TASKS_FORCE_SERIAL is set in this environment: the pooled "
           "contraction would run serially and this determinism test would be "
           "vacuous";

    Rng rng{ 0xBEEF1234u };
    const std::vector<uint32_t> leaf_dims = { 2, 3, 2, 2, 3, 2, 2, 2 };  // 8 leaves

    for (int trial = 0; trial < 20; ++trial) {
        SharedEdgePolytreeBuilder<TreeNode, BondEnv> b;

        std::size_t centre_sz = 2;
        for (uint32_t d : leaf_dims) {
            centre_sz *= d;
        }
        std::vector<Complex> ct(centre_sz);
        fill_random(ct, rng);
        const NodeHandle centre = add_node(b, tn_node(1, leaf_dims, ct));

        for (uint32_t d : leaf_dims) {
            std::vector<Complex> lt(static_cast<std::size_t>(2) * d);
            fill_random(lt, rng);
            const NodeHandle leaf = add_node(b, tn_node(d, {}, std::move(lt)));
            add_edge(b, centre, leaf, BondEnv{});
        }

        TreeTnNetwork net = std::move(*build(std::move(b)));

        const std::vector<double> serial = tree_tn_sigma_z(net);

        std::vector<double> pooled;
        {
            ScopedPool guard{ 4 };
            pooled = tree_tn_sigma_z(net);
        }

        ASSERT_EQ(serial.size(), pooled.size());
        for (std::size_t i = 0; i < serial.size(); ++i) {
            ASSERT_EQ(serial[i], pooled[i])  // bit-exact, deliberately not EXPECT_NEAR
                << "trial " << trial << " site " << i;
        }
    }
}

// S3 (#293): the result is independent of the parallelism DEGREE, not just of
// serial-vs-parallel -- the determinism guarantee the ladder locks. The same
// contraction under 1, 2, and 8 workers is bit-identical to serial.
TEST(TreeTn, PoolResultIsWorkerCountInvariant)
{
    ASSERT_FALSE(wz::tasks::force_serial())
        << "WZ_TASKS_FORCE_SERIAL is set: every worker count below would run "
           "serially, so the invariance this asserts would be trivial";

    Rng rng{ 0x515Eu };
    const std::vector<uint32_t> leaf_dims = { 2, 3, 2, 2, 3, 2 };

    SharedEdgePolytreeBuilder<TreeNode, BondEnv> b;
    std::size_t centre_sz = 2;
    for (uint32_t d : leaf_dims) {
        centre_sz *= d;
    }
    std::vector<Complex> ct(centre_sz);
    fill_random(ct, rng);
    const NodeHandle centre = add_node(b, tn_node(1, leaf_dims, ct));
    for (uint32_t d : leaf_dims) {
        std::vector<Complex> lt(static_cast<std::size_t>(2) * d);
        fill_random(lt, rng);
        const NodeHandle leaf = add_node(b, tn_node(d, {}, std::move(lt)));
        add_edge(b, centre, leaf, BondEnv{});
    }
    TreeTnNetwork net = std::move(*build(std::move(b)));

    const std::vector<double> serial = tree_tn_sigma_z(net);

    for (unsigned workers : { 1u, 2u, 8u }) {
        ScopedPool guard{ workers };
        const std::vector<double> pooled = tree_tn_sigma_z(net);

        ASSERT_EQ(serial.size(), pooled.size());
        for (std::size_t i = 0; i < serial.size(); ++i) {
            ASSERT_EQ(serial[i], pooled[i])
                << "workers " << workers << " site " << i;
        }
    }
}
