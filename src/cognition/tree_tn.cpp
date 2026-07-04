#include <cognition/tree_tn.h>

#include <cognition/tensor_leg.h>
#include <graph/shared_edge_polytree_algo.h>

#include <complex>

namespace wz::engine::cognition
{
    namespace
    {
        using wz::engine::cognition::qstate::Complex;
        using wz::core::graph::INVALID_NODE;
        using wz::core::graph::NodeHandle;

        // ─── one-at-a-time message folding ────────────────────────────────────
        //
        // The tensors here have layout [2, Dp, c_0, c_1, ...] (mode 0 = physical
        // dim 2, mode 1 = parent bond Dp, mode 2+k = child bond c_k). The old code
        // ran a JOINT odometer over BOTH the ket child multi-index and the bra
        // child multi-index -- O(chi^(2K)) for K child bonds. Instead we fold each
        // neighbor message into a working KET copy ONE leg at a time (never holding
        // a tensor with all K ket AND all K bra legs open at once), then contract
        // the fully-absorbed ket against the untouched bra tensor conj(T). Peak
        // cost is O(chi^(K+1)); no intermediate ever carries 2K bond legs.
        //
        // absorb_leg (the tensor-times-matrix leg fold) is the shared kernel in
        // cognition/tensor_leg.h; mode_dims stays here because it takes a TreeNode.

        // The full mode-dimension list [2, Dp, c_0, ...] for a node's tensor.
        std::vector<uint32_t> mode_dims(const TreeNode& T)
        {
            std::vector<uint32_t> dims;
            dims.reserve(2 + T.child_bonds.size());
            dims.push_back(2);
            dims.push_back(T.parent_bond);
            for (uint32_t d : T.child_bonds) {
                dims.push_back(d);
            }
            return dims;
        }

        // Up-message on the parent bond (parent_bond^2), folding in every child's
        // up-message:
        //   Mp[a][a'] = sum_{s, b, b'} T[s][a][b] conj(T[s][a'][b']) prod_k Cu_k[b_k][b'_k]
        //
        // Method: absorb each child's up-message into a ket copy of T (turning each
        // child leg into a bra index), then contract s and every child leg against
        // conj(T), leaving the parent leg free on both sides.
        std::vector<Complex> up_message(
            const TreeNode& T, const std::vector<std::vector<Complex>>& cu)
        {
            const uint32_t Dp = T.parent_bond;
            const std::size_t K = T.child_bonds.size();
            const std::vector<uint32_t> dims = mode_dims(T);

            std::vector<Complex> W = T.t;  // ket working copy
            for (std::size_t k = 0; k < K; ++k) {
                absorb_leg(W, dims, 2 + k, cu[k]);  // child legs start at mode 2
            }

            // Now W has layout [2, Dp(ket a), c_0(bra b'_0), ...]; conj(T) has
            // [2, Dp(bra a'), c_0(bra b'_0), ...]. Contract s and all child legs
            // (they share the bra multi-index) leaving a (from W) and a' (from T).
            std::vector<Complex> mp(static_cast<std::size_t>(Dp) * Dp,
                Complex{ 0, 0 });
            std::size_t child_span = 1;  // prod of child dims
            for (uint32_t d : T.child_bonds) {
                child_span *= d;
            }
            for (uint32_t s = 0; s < 2; ++s) {
                const std::size_t s_base = static_cast<std::size_t>(s) * Dp;
                for (uint32_t a = 0; a < Dp; ++a) {
                    const std::size_t w_base = (s_base + a) * child_span;
                    for (uint32_t ap = 0; ap < Dp; ++ap) {
                        const std::size_t t_base = (s_base + ap) * child_span;
                        Complex acc{ 0, 0 };
                        for (std::size_t j = 0; j < child_span; ++j) {
                            acc += W[w_base + j] * std::conj(T.t[t_base + j]);
                        }
                        mp[static_cast<std::size_t>(a) * Dp + ap] += acc;
                    }
                }
            }
            return mp;
        }

        // Down-message toward child `c` (child_bonds[c]^2): folds in the parent's
        // down-message and every OTHER child's up-message; child c's bond stays
        // free (it indexes the result).
        //
        // Method: absorb parent_down along the parent leg and every child up-
        // message EXCEPT c's into a ket copy of T, then contract against conj(T)
        // over s, the parent leg, and every child leg != c, leaving W's leg-c (ket
        // b_c) and conj(T)'s leg-c (bra b'_c) free.
        std::vector<Complex> down_message_to(
            const TreeNode& T, uint32_t c,
            const std::vector<std::vector<Complex>>& cu,
            const std::vector<Complex>& parent_down)
        {
            const uint32_t Dp = T.parent_bond;
            const uint32_t Dc = T.child_bonds[c];
            const std::size_t K = T.child_bonds.size();
            const std::vector<uint32_t> dims = mode_dims(T);

            std::vector<Complex> W = T.t;  // ket working copy
            absorb_leg(W, dims, 1, parent_down);  // parent leg is mode 1
            for (std::size_t k = 0; k < K; ++k) {
                if (k == c) {
                    continue;  // child c's bond stays free
                }
                absorb_leg(W, dims, 2 + k, cu[k]);
            }

            // Strides so we can iterate: outer legs before c, leg c, inner after c.
            std::size_t inner_c = 1;  // product of child dims after c
            for (std::size_t k = c + 1; k < K; ++k) {
                inner_c *= T.child_bonds[k];
            }
            // Everything strictly left of leg c collapses to a single contracted
            // sum: modes [s, parent, c_0..c_{c-1}]. Their combined extent:
            std::size_t left_span = static_cast<std::size_t>(2) * Dp;
            for (std::size_t k = 0; k < c; ++k) {
                left_span *= T.child_bonds[k];
            }
            const std::size_t c_block = static_cast<std::size_t>(Dc) * inner_c;

            std::vector<Complex> md(static_cast<std::size_t>(Dc) * Dc,
                Complex{ 0, 0 });
            for (std::size_t left = 0; left < left_span; ++left) {
                const std::size_t left_base = left * c_block;
                for (uint32_t bc = 0; bc < Dc; ++bc) {          // ket b_c from W
                    const std::size_t w_base = left_base + bc * inner_c;
                    for (uint32_t bpc = 0; bpc < Dc; ++bpc) {   // bra b'_c from T
                        const std::size_t t_base = left_base + bpc * inner_c;
                        Complex acc{ 0, 0 };
                        for (std::size_t j = 0; j < inner_c; ++j) {
                            acc += W[w_base + j] * std::conj(T.t[t_base + j]);
                        }
                        md[static_cast<std::size_t>(bc) * Dc + bpc] += acc;
                    }
                }
            }
            return md;
        }

        double marginal_sigma_z(
            const TreeNode& T,
            const std::vector<std::vector<Complex>>& cu,
            const std::vector<Complex>& parent_down)
        {
            const uint32_t Dp = T.parent_bond;
            const std::size_t K = T.child_bonds.size();
            const std::vector<uint32_t> dims = mode_dims(T);

            std::vector<Complex> W = T.t;  // ket working copy
            absorb_leg(W, dims, 1, parent_down);  // parent leg is mode 1
            for (std::size_t k = 0; k < K; ++k) {
                absorb_leg(W, dims, 2 + k, cu[k]);
            }

            // W now has layout [2, Dp(bra a'), child bras]; contract everything
            // except s against conj(T), keeping s free.
            std::size_t tail = static_cast<std::size_t>(Dp);  // parent + child span
            for (uint32_t d : T.child_bonds) {
                tail *= d;
            }
            Complex rho[2] = { Complex{ 0, 0 }, Complex{ 0, 0 } };  // diagonal
            for (uint32_t s = 0; s < 2; ++s) {
                const std::size_t base = static_cast<std::size_t>(s) * tail;
                Complex acc{ 0, 0 };
                for (std::size_t j = 0; j < tail; ++j) {
                    acc += W[base + j] * std::conj(T.t[base + j]);
                }
                rho[s] = acc;
            }

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
