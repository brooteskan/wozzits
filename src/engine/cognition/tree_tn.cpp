#include <engine/cognition/tree_tn.h>

#include <graph/shared_edge_polytree_algo.h>

#include <complex>

namespace wz::cognition
{
    namespace
    {
        using wz::qstate::Complex;
        using wz::core::graph::INVALID_NODE;
        using wz::core::graph::NodeHandle;

        // Flat tensor index for layout [2, parent_bond, child_bonds...].
        std::size_t flat_index(
            const TreeNode& T, uint32_t s, uint32_t a,
            const std::vector<uint32_t>& b)
        {
            std::size_t idx = s;
            idx = idx * T.parent_bond + a;
            for (std::size_t k = 0; k < b.size(); ++k) {
                idx = idx * T.child_bonds[k] + b[k];
            }
            return idx;
        }

        // Advance a child multi-index; returns false when it wraps (so a do/while
        // runs exactly once for a leaf, whose dims are empty).
        bool odometer_next(
            std::vector<uint32_t>& b, const std::vector<uint32_t>& dims)
        {
            for (std::size_t k = 0; k < b.size(); ++k) {
                if (++b[k] < dims[k]) {
                    return true;
                }
                b[k] = 0;
            }
            return false;
        }

        // Up-message on the parent bond (parent_bond^2), folding in every child's
        // up-message:
        //   Mp[a][a'] = sum_{s, b, b'} T[s][a][b] conj(T[s][a'][b']) prod_k Cu_k[b_k][b'_k]
        std::vector<Complex> up_message(
            const TreeNode& T, const std::vector<std::vector<Complex>>& cu)
        {
            const uint32_t Dp = T.parent_bond;
            const std::size_t K = T.child_bonds.size();
            std::vector<Complex> mp(static_cast<std::size_t>(Dp) * Dp,
                Complex{ 0, 0 });

            std::vector<uint32_t> b(K, 0);
            do {
                std::vector<uint32_t> bp(K, 0);
                do {
                    Complex w{ 1, 0 };
                    for (std::size_t k = 0; k < K; ++k) {
                        w *= cu[k][static_cast<std::size_t>(b[k]) * T.child_bonds[k]
                            + bp[k]];
                    }
                    for (uint32_t s = 0; s < 2; ++s) {
                        for (uint32_t a = 0; a < Dp; ++a) {
                            const Complex ta = T.t[flat_index(T, s, a, b)];
                            for (uint32_t ap = 0; ap < Dp; ++ap) {
                                mp[static_cast<std::size_t>(a) * Dp + ap] +=
                                    ta * std::conj(T.t[flat_index(T, s, ap, bp)]) * w;
                            }
                        }
                    }
                } while (odometer_next(bp, T.child_bonds));
            } while (odometer_next(b, T.child_bonds));
            return mp;
        }

        // Down-message toward child `c` (child_bonds[c]^2): folds in the parent's
        // down-message and every OTHER child's up-message; child c's bond stays
        // free (it indexes the result).
        std::vector<Complex> down_message_to(
            const TreeNode& T, uint32_t c,
            const std::vector<std::vector<Complex>>& cu,
            const std::vector<Complex>& parent_down)
        {
            const uint32_t Dp = T.parent_bond;
            const uint32_t Dc = T.child_bonds[c];
            const std::size_t K = T.child_bonds.size();
            std::vector<Complex> md(static_cast<std::size_t>(Dc) * Dc,
                Complex{ 0, 0 });

            std::vector<uint32_t> b(K, 0);
            do {
                std::vector<uint32_t> bp(K, 0);
                do {
                    Complex w{ 1, 0 };
                    for (std::size_t k = 0; k < K; ++k) {
                        if (k == c) {
                            continue;  // child c's bond is left free
                        }
                        w *= cu[k][static_cast<std::size_t>(b[k]) * T.child_bonds[k]
                            + bp[k]];
                    }
                    const std::size_t out =
                        static_cast<std::size_t>(b[c]) * Dc + bp[c];
                    for (uint32_t s = 0; s < 2; ++s) {
                        for (uint32_t a = 0; a < Dp; ++a) {
                            for (uint32_t ap = 0; ap < Dp; ++ap) {
                                md[out] +=
                                    parent_down[static_cast<std::size_t>(a) * Dp + ap]
                                    * T.t[flat_index(T, s, a, b)]
                                    * std::conj(T.t[flat_index(T, s, ap, bp)]) * w;
                            }
                        }
                    }
                } while (odometer_next(bp, T.child_bonds));
            } while (odometer_next(b, T.child_bonds));
            return md;
        }

        double marginal_sigma_z(
            const TreeNode& T,
            const std::vector<std::vector<Complex>>& cu,
            const std::vector<Complex>& parent_down)
        {
            const uint32_t Dp = T.parent_bond;
            const std::size_t K = T.child_bonds.size();
            Complex rho[2] = { Complex{ 0, 0 }, Complex{ 0, 0 } };  // diagonal

            std::vector<uint32_t> b(K, 0);
            do {
                std::vector<uint32_t> bp(K, 0);
                do {
                    Complex w{ 1, 0 };
                    for (std::size_t k = 0; k < K; ++k) {
                        w *= cu[k][static_cast<std::size_t>(b[k]) * T.child_bonds[k]
                            + bp[k]];
                    }
                    for (uint32_t s = 0; s < 2; ++s) {
                        for (uint32_t a = 0; a < Dp; ++a) {
                            for (uint32_t ap = 0; ap < Dp; ++ap) {
                                rho[s] +=
                                    parent_down[static_cast<std::size_t>(a) * Dp + ap]
                                    * T.t[flat_index(T, s, a, b)]
                                    * std::conj(T.t[flat_index(T, s, ap, bp)]) * w;
                            }
                        }
                    }
                } while (odometer_next(bp, T.child_bonds));
            } while (odometer_next(b, T.child_bonds));

            const double p0 = rho[0].real();
            const double p1 = rho[1].real();
            const double total = p0 + p1;
            return total > 0 ? (p0 - p1) / total : 0.0;
        }
    }

    std::vector<double> tree_tn_sigma_z(TreeTnNetwork& net)
    {
        using namespace wz::core::graph;

        bp_collect(net, [&](NodeHandle n, NodeHandle p, BondEnv& e_parent) {
            const TreeNode& T = node_data(net, n);
            std::vector<std::vector<Complex>> cu;  // children's up-messages, in order
            for_each_neighbor_except(net, n, p,
                [&](NodeHandle, BondEnv& e_child) { cu.push_back(e_child.up); });
            e_parent.dim = T.parent_bond;
            e_parent.up = up_message(T, cu);
        });

        bp_distribute(net, [&](NodeHandle n, NodeHandle child, BondEnv& e_child) {
            const TreeNode& T = node_data(net, n);
            const NodeHandle p = parent(net, n);

            std::vector<std::vector<Complex>> cu;
            uint32_t c_index = 0;
            uint32_t k = 0;
            for_each_neighbor_except(net, n, p, [&](NodeHandle nb, BondEnv& ec) {
                cu.push_back(ec.up);
                if (nb == child) {
                    c_index = k;
                }
                ++k;
            });

            std::vector<Complex> parent_down = { Complex{ 1, 0 } };
            if (p != INVALID_NODE) {
                parent_down = edge_to_parent(net, n).down;
            }
            e_child.dim = T.child_bonds[c_index];
            e_child.down = down_message_to(T, c_index, cu, parent_down);
        });

        std::vector<double> sz(node_count(net), 0.0);
        for (NodeHandle n = 0; n < node_count(net); ++n) {
            const TreeNode& T = node_data(net, n);
            const NodeHandle p = parent(net, n);

            std::vector<std::vector<Complex>> cu;
            for_each_neighbor_except(net, n, p,
                [&](NodeHandle, BondEnv& ec) { cu.push_back(ec.up); });

            std::vector<Complex> parent_down = { Complex{ 1, 0 } };
            if (p != INVALID_NODE) {
                parent_down = edge_to_parent(net, n).down;
            }
            sz[n] = marginal_sigma_z(T, cu, parent_down);
        }
        return sz;
    }
}
