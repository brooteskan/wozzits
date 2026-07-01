#include <cognition/tree_bp.h>

#include <graph/shared_edge_polytree_algo.h>

#include <complex>

namespace wz::engine::cognition
{
    namespace
    {
        using wz::engine::cognition::qstate::Complex;
        using wz::core::graph::INVALID_NODE;
        using wz::core::graph::NodeHandle;

        // A[(s*L + l)*R + r]
        inline Complex site_at(
            const MpsSite& A, uint32_t s, uint32_t l, uint32_t r)
        {
            return A.a[(static_cast<std::size_t>(s) * A.left + l) * A.right + r];
        }

        // Up-message (right environment on the parent bond), L x L, from the
        // site and the child's up-message Mr (R x R; for a leaf, the 1x1 [1]).
        //   Mp[a][a'] = sum_{s,b,b'} A[s][a][b] conj(A[s][a'][b']) Mr[b][b']
        std::vector<Complex> up_message(
            const MpsSite& A, const std::vector<Complex>& Mr, uint32_t R)
        {
            const uint32_t L = A.left;
            std::vector<Complex> Mp(static_cast<std::size_t>(L) * L,
                Complex{ 0, 0 });
            for (uint32_t a = 0; a < L; ++a) {
                for (uint32_t ap = 0; ap < L; ++ap) {
                    Complex acc{ 0, 0 };
                    for (uint32_t s = 0; s < 2; ++s) {
                        for (uint32_t b = 0; b < A.right; ++b) {
                            for (uint32_t bp = 0; bp < A.right; ++bp) {
                                acc += site_at(A, s, a, b)
                                    * std::conj(site_at(A, s, ap, bp))
                                    * Mr[static_cast<std::size_t>(b) * R + bp];
                            }
                        }
                    }
                    Mp[static_cast<std::size_t>(a) * L + ap] = acc;
                }
            }
            return Mp;
        }

        // Down-message (left environment on a child bond), R x R, from the site
        // and the parent's down-message Ml (L x L; for the root, the 1x1 [1]).
        //   Md[b][b'] = sum_{s,a,a'} A[s][a][b] conj(A[s][a'][b']) Ml[a][a']
        std::vector<Complex> down_message(
            const MpsSite& A, const std::vector<Complex>& Ml, uint32_t L)
        {
            const uint32_t R = A.right;
            std::vector<Complex> Md(static_cast<std::size_t>(R) * R,
                Complex{ 0, 0 });
            for (uint32_t b = 0; b < R; ++b) {
                for (uint32_t bp = 0; bp < R; ++bp) {
                    Complex acc{ 0, 0 };
                    for (uint32_t s = 0; s < 2; ++s) {
                        for (uint32_t a = 0; a < A.left; ++a) {
                            for (uint32_t ap = 0; ap < A.left; ++ap) {
                                acc += site_at(A, s, a, b)
                                    * std::conj(site_at(A, s, ap, bp))
                                    * Ml[static_cast<std::size_t>(a) * L + ap];
                            }
                        }
                    }
                    Md[static_cast<std::size_t>(b) * R + bp] = acc;
                }
            }
            return Md;
        }

        // <sigma_z> from the site and its left/right environments.
        //   rho[s][s'] = sum Ml[a][a'] A[s][a][b] conj(A[s'][a'][b']) Mr[b][b']
        double sigma_z_from_envs(
            const MpsSite& A,
            const std::vector<Complex>& Ml, uint32_t L,
            const std::vector<Complex>& Mr, uint32_t R)
        {
            Complex rho[2] = { Complex{ 0, 0 }, Complex{ 0, 0 } };  // diagonal
            for (uint32_t s = 0; s < 2; ++s) {
                for (uint32_t a = 0; a < A.left; ++a) {
                    for (uint32_t ap = 0; ap < A.left; ++ap) {
                        for (uint32_t b = 0; b < A.right; ++b) {
                            for (uint32_t bp = 0; bp < A.right; ++bp) {
                                rho[s] += Ml[static_cast<std::size_t>(a) * L + ap]
                                    * site_at(A, s, a, b)
                                    * std::conj(site_at(A, s, ap, bp))
                                    * Mr[static_cast<std::size_t>(b) * R + bp];
                            }
                        }
                    }
                }
            }
            const double p0 = rho[0].real();
            const double p1 = rho[1].real();
            const double total = p0 + p1;
            return total > 0 ? (p0 - p1) / total : 0.0;
        }
    }

    std::vector<double> tree_bp_sigma_z(TreeBpNetwork& net)
    {
        using namespace wz::core::graph;

        // bp_collect (leaf -> root): up-message = right environment, written onto
        // each non-root node's parent edge, combining its child's up-message.
        bp_collect(net, [&](NodeHandle n, NodeHandle p, BondEnv& e_parent) {
            const MpsSite& A = node_data(net, n);
            std::vector<Complex> child_up = { Complex{ 1, 0 } };  // leaf boundary
            uint32_t rdim = 1;
            for_each_neighbor_except(net, n, p,
                [&](NodeHandle, BondEnv& e_child) {
                    child_up = e_child.up;
                    rdim = e_child.dim;
                });
            e_parent.dim = A.left;
            e_parent.up = up_message(A, child_up, rdim);
        });

        // bp_distribute (root -> leaf): down-message = left environment, written
        // onto each child edge, combining the parent's down-message.
        bp_distribute(net, [&](NodeHandle n, NodeHandle child, BondEnv& e_child) {
            const MpsSite& A = node_data(net, n);
            std::vector<Complex> parent_down = { Complex{ 1, 0 } };  // root boundary
            uint32_t ldim = 1;
            if (parent(net, n) != INVALID_NODE) {
                const BondEnv& e_parent = edge_to_parent(net, n);
                parent_down = e_parent.down;
                ldim = e_parent.dim;
            }
            (void)child;
            e_child.dim = A.right;
            e_child.down = down_message(A, parent_down, ldim);
        });

        // Each node's marginal: its tensor contracted with both environments.
        std::vector<double> sz(node_count(net), 0.0);
        for (NodeHandle n = 0; n < node_count(net); ++n) {
            const MpsSite& A = node_data(net, n);

            std::vector<Complex> Ml = { Complex{ 1, 0 } };
            uint32_t ldim = 1;
            if (parent(net, n) != INVALID_NODE) {
                Ml = edge_to_parent(net, n).down;
                ldim = edge_to_parent(net, n).dim;
            }

            std::vector<Complex> Mr = { Complex{ 1, 0 } };
            uint32_t rdim = 1;
            for_each_neighbor_except(net, n, parent(net, n),
                [&](NodeHandle, BondEnv& e_child) {
                    Mr = e_child.up;
                    rdim = e_child.dim;
                });

            sz[n] = sigma_z_from_envs(A, Ml, ldim, Mr, rdim);
        }
        return sz;
    }

    void apply_two_site_gate(
        MpsSite& A,
        MpsSite& B,
        const std::vector<Complex>& gate,
        uint32_t chi)
    {
        const uint32_t L = A.left;
        const uint32_t M = A.right;  // shared bond, == B.left
        const uint32_t R = B.right;

        // Theta[l][s0][s1][r] = sum_m A[s0][l][m] B[s1][m][r].
        // Flat index ((l*2 + s0)*2 + s1)*R + r.
        std::vector<Complex> theta(
            static_cast<std::size_t>(L) * 2 * 2 * R, Complex{ 0, 0 });
        for (uint32_t l = 0; l < L; ++l) {
            for (uint32_t s0 = 0; s0 < 2; ++s0) {
                for (uint32_t s1 = 0; s1 < 2; ++s1) {
                    for (uint32_t r = 0; r < R; ++r) {
                        Complex acc{ 0, 0 };
                        for (uint32_t m = 0; m < M; ++m) {
                            acc += A.a[(static_cast<std::size_t>(s0) * L + l) * M + m]
                                * B.a[(static_cast<std::size_t>(s1) * M + m) * R + r];
                        }
                        theta[((static_cast<std::size_t>(l) * 2 + s0) * 2 + s1) * R + r]
                            = acc;
                    }
                }
            }
        }

        // Apply the gate and reshape into Mmat[(l*2 + s0')][(s1'*R + r)],
        // dims (L*2) x (2*R).
        const uint32_t rows = L * 2;
        const uint32_t cols = 2 * R;
        std::vector<Complex> mmat(
            static_cast<std::size_t>(rows) * cols, Complex{ 0, 0 });
        for (uint32_t l = 0; l < L; ++l) {
            for (uint32_t s0p = 0; s0p < 2; ++s0p) {
                for (uint32_t s1p = 0; s1p < 2; ++s1p) {
                    for (uint32_t r = 0; r < R; ++r) {
                        Complex acc{ 0, 0 };
                        for (uint32_t s0 = 0; s0 < 2; ++s0) {
                            for (uint32_t s1 = 0; s1 < 2; ++s1) {
                                acc += gate[(s0p * 2 + s1p) * 4 + (s0 * 2 + s1)]
                                    * theta[((static_cast<std::size_t>(l) * 2 + s0) * 2
                                                + s1)
                                            * R
                                        + r];
                            }
                        }
                        mmat[(static_cast<std::size_t>(l) * 2 + s0p) * cols
                            + (static_cast<std::size_t>(s1p) * R + r)] = acc;
                    }
                }
            }
        }

        const wz::engine::cognition::qstate::Svd d = wz::engine::cognition::qstate::svd(mmat, rows, cols, chi);
        const uint32_t k = d.rank;

        // New left site: A'[s0'][l][m'] = U[(l*2 + s0')][m'].  left bond L, right k.
        A.left = L;
        A.right = k;
        A.a.assign(static_cast<std::size_t>(2) * L * k, Complex{ 0, 0 });
        for (uint32_t s0p = 0; s0p < 2; ++s0p) {
            for (uint32_t l = 0; l < L; ++l) {
                for (uint32_t mp = 0; mp < k; ++mp) {
                    A.a[(static_cast<std::size_t>(s0p) * L + l) * k + mp] =
                        d.u[(static_cast<std::size_t>(l) * 2 + s0p) * k + mp];
                }
            }
        }

        // New right site: B'[s1'][m'][r] = s[m'] * Vh[m'][(s1'*R + r)]. left k, right R.
        B.left = k;
        B.right = R;
        B.a.assign(static_cast<std::size_t>(2) * k * R, Complex{ 0, 0 });
        for (uint32_t s1p = 0; s1p < 2; ++s1p) {
            for (uint32_t mp = 0; mp < k; ++mp) {
                for (uint32_t r = 0; r < R; ++r) {
                    B.a[(static_cast<std::size_t>(s1p) * k + mp) * R + r] =
                        d.s[mp]
                        * d.vh[static_cast<std::size_t>(mp) * cols
                            + (static_cast<std::size_t>(s1p) * R + r)];
                }
            }
        }
    }

    void apply_one_site_gate(MpsSite& A, const std::vector<Complex>& g)
    {
        const uint32_t L = A.left;
        const uint32_t R = A.right;
        for (uint32_t l = 0; l < L; ++l) {
            for (uint32_t r = 0; r < R; ++r) {
                const std::size_t i0 = (static_cast<std::size_t>(0) * L + l) * R + r;
                const std::size_t i1 = (static_cast<std::size_t>(1) * L + l) * R + r;
                const Complex a0 = A.a[i0];
                const Complex a1 = A.a[i1];
                A.a[i0] = g[0] * a0 + g[1] * a1;
                A.a[i1] = g[2] * a0 + g[3] * a1;
            }
        }
    }

    std::vector<double> dense_sigma_z(const TreeBpNetwork& net)
    {
        using namespace wz::core::graph;
        const auto order = topo_order(net);  // chain order: parents precede children

        // Contract the chain left-to-right into the full statevector. psi is
        // indexed by (config so far, current open right bond); config's first
        // contracted site is the most-significant bit.
        std::vector<Complex> psi = { Complex{ 1, 0 } };  // scalar left boundary
        uint32_t bond = 1;
        std::size_t nconf = 1;
        for (const NodeHandle n : order) {
            const MpsSite& A = node_data(net, n);
            std::vector<Complex> np(nconf * 2u * A.right, Complex{ 0, 0 });
            for (std::size_t c = 0; c < nconf; ++c) {
                for (uint32_t s = 0; s < 2; ++s) {
                    for (uint32_t r = 0; r < A.right; ++r) {
                        Complex acc{ 0, 0 };
                        for (uint32_t l = 0; l < A.left; ++l) {
                            acc += psi[c * bond + l] * site_at(A, s, l, r);
                        }
                        np[(c * 2u + s) * A.right + r] = acc;
                    }
                }
            }
            psi = std::move(np);
            nconf *= 2u;
            bond = A.right;  // 1 at the leaf
        }

        // psi[config] (trailing bond dim 1). <sigma_z> per site from |psi|^2.
        const uint32_t nsites = static_cast<uint32_t>(order.size());
        double total = 0;
        for (std::size_t c = 0; c < nconf; ++c) {
            total += std::norm(psi[c]);
        }

        std::vector<double> sz(node_count(net), 0.0);
        for (uint32_t pos = 0; pos < nsites; ++pos) {
            const uint32_t bit = nsites - 1u - pos;  // site at topo pos -> bit
            double acc = 0;
            for (std::size_t c = 0; c < nconf; ++c) {
                const double sign = ((c >> bit) & 1u) ? -1.0 : 1.0;
                acc += sign * std::norm(psi[c]);
            }
            sz[order[pos]] = total > 0 ? acc / total : 0.0;
        }
        return sz;
    }
}
