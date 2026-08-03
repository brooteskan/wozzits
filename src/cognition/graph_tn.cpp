#include <cognition/graph_tn.h>

#include <cognition/group_topology.h>
#include <cognition/tensor_leg.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace wz::engine::cognition
{
    namespace
    {
        using wz::engine::cognition::qstate::Complex;
        using wz::core::graph::NodeHandle;

        // Regularization floor for the lambda inverse in the environment divide-
        // back (step 7). A near-zero Schmidt value multiplied in during absorb
        // must not blow up when divided back out; clamping the divisor keeps the
        // inverse bounded. Values this small carry negligible weight, so clamping
        // (rather than a true 1/lambda) does not perturb the physical state.
        constexpr double kLambdaInvFloor = 1e-12;

        // Canonicalization passes used to propagate a projection outward from a
        // collapsed node (see collapse). One pass is a full forward-then-backward
        // sweep over every edge, which is exact on a tree; the second settles the
        // gauge on loops. Matches what relax_step already spends per substep.
        constexpr uint32_t kConditioningPasses = 2u;

        // The full mode-dimension list [2, bond_0, bond_1, ...] for a site tensor.
        std::vector<uint32_t> site_dims(const GraphSite& s)
        {
            std::vector<uint32_t> dims;
            dims.reserve(1 + s.bond_dim.size());
            dims.push_back(2);
            for (uint32_t d : s.bond_dim) {
                dims.push_back(d);
            }
            return dims;
        }

        // The incidences of node u: the ORDERED neighbor handles and the ORDERED
        // edge-slot pointers, in for_each_neighbor iteration order (which is u's
        // canonical bond-leg order). Gathered so we can index a specific leg and
        // absorb every OTHER leg's lambda.
        //
        // Small-buffer backed: a cognition node's degree is a handful (a squad
        // member's neighbours), and this is gathered twice per edge gate, which
        // runs per bond per substep. Two heap allocations per call there is pure
        // overhead on the hot path.
        struct Incidences
        {
            static constexpr std::size_t kInline = 8;
            NodeHandle neighbor_buf[kInline] = {};
            GraphBond* bond_buf[kInline] = {};
            std::vector<NodeHandle> neighbor_heap;   // used only past kInline
            std::vector<GraphBond*> bond_heap;
            std::size_t count = 0;

            bool spilled() const { return count > kInline; }
            NodeHandle neighbor(std::size_t k) const
            {
                return spilled() ? neighbor_heap[k] : neighbor_buf[k];
            }
            GraphBond* bond(std::size_t k) const
            {
                return spilled() ? bond_heap[k] : bond_buf[k];
            }
            std::size_t size() const { return count; }
        };

        Incidences incidences_of(
            wz::core::graph::SharedEdgeGraph<GraphSite, GraphBond>& graph,
            NodeHandle u)
        {
            Incidences inc;
            for_each_neighbor(graph, u, [&](NodeHandle nb, GraphBond& e) {
                if (inc.count < Incidences::kInline) {
                    inc.neighbor_buf[inc.count] = nb;
                    inc.bond_buf[inc.count] = &e;
                } else {
                    if (inc.count == Incidences::kInline) {
                        // Spill: copy the inline prefix out, then grow on the heap.
                        inc.neighbor_heap.assign(
                            inc.neighbor_buf, inc.neighbor_buf + Incidences::kInline);
                        inc.bond_heap.assign(
                            inc.bond_buf, inc.bond_buf + Incidences::kInline);
                    }
                    inc.neighbor_heap.push_back(nb);
                    inc.bond_heap.push_back(&e);
                }
                ++inc.count;
            });
            return inc;
        }

        // The 2x2 imaginary-time single-site field matrix e^{-dtau (-h sigma_z -
        // gamma sigma_x)} in the absorb_leg [ket*2 + bra] layout, matching
        // qstate::apply_imag_time_field's construction exactly. Real-valued for
        // this Hamiltonian; identity when the field is zero.
        std::vector<Complex> field_matrix(double gamma, double h, double dtau)
        {
            // H = -h sigma_z - gamma sigma_x = [[-h, -gamma], [-gamma, +h]].
            // H^2 = (h^2 + gamma^2) I = E^2 I, so
            //   e^{-H dtau} = cosh(E dtau) I - (sinh(E dtau)/E) H.
            // TANH FORM -- see the long note in qstate::apply_imag_time_field.
            // cosh(E dtau) overflows at ~710 and its SQUARE at ~355; the gate is
            // renormalized after, so dividing through by cosh(E dtau) gives
            // I - tanh(E dtau) H/E, whose entries are in [-2, 2] for any finite
            // input and whose limit is the same ground-state projector.
            // Guard the INPUTS, not the result: testing isfinite afterwards would
            // raise a spurious FE_OVERFLOW on the way to deciding not to trust it.
            constexpr double kSquareSafe = 1e150;
            const double e =
                (std::abs(h) < kSquareSafe && std::abs(gamma) < kSquareSafe)
                    ? std::sqrt(h * h + gamma * gamma)
                    : std::hypot(h, gamma);
            std::vector<Complex> m(4, Complex{ 0, 0 });
            // Reject direction: `e <= 0.0` admitted a NaN field.
            if (!(e > 0.0) || !std::isfinite(e) || !std::isfinite(dtau)) {
                m[0] = Complex{ 1, 0 };  // H == 0, or no defined evolution
                m[3] = Complex{ 1, 0 };
                return m;
            }
            const double th = std::tanh(e * dtau);
            m[0] = Complex{ 1.0 + th * (h / e), 0 };
            m[1] = Complex{ th * (gamma / e), 0 };
            m[2] = Complex{ th * (gamma / e), 0 };
            m[3] = Complex{ 1.0 - th * (h / e), 0 };
            return m;
        }

        // The 4x4 imaginary-time ZZ gate e^{-dtau (-j sigma_z sigma_z)} =
        // e^{+j dtau sigma_z sigma_z}, indexed [out*4 + in] with out = s0'*2 + s1',
        // in = s0*2 + s1 -- the same convention apply_two_site_gate consumes.
        // Diagonal: +1 eigenvalue when the two bits agree (weight e^{+j dtau}),
        // -1 when they differ (weight e^{-j dtau}). Matches qstate::apply_imag_
        // time_zz's per-basis weighting.
        std::vector<Complex> zz_gate(double j, double dtau)
        {
            // Scaled by e^{-|j dtau|} so both eigenvalues land in (0, 1] rather
            // than overflowing past |j dtau| ~ 709; renormalization after makes
            // the common factor immaterial. Matches qstate::apply_imag_time_zz.
            const double x = j * dtau;
            const double sane = std::isfinite(x) ? x : 0.0;
            const double decay = std::exp(-2.0 * std::abs(sane));
            const double agree = sane >= 0 ? 1.0 : decay;
            const double differ = sane >= 0 ? decay : 1.0;
            std::vector<Complex> g(16, Complex{ 0, 0 });
            for (uint32_t s0 = 0; s0 < 2; ++s0) {
                for (uint32_t s1 = 0; s1 < 2; ++s1) {
                    const uint32_t idx = s0 * 2 + s1;
                    g[idx * 4 + idx] =
                        Complex{ (s0 == s1) ? agree : differ, 0 };
                }
            }
            return g;
        }

        // The 4x4 identity two-qubit gate (used by the canonicalization sweep: an
        // identity update re-SVDs a bond without touching the physical state).
        std::vector<Complex> identity_gate()
        {
            std::vector<Complex> g(16, Complex{ 0, 0 });
            for (uint32_t i = 0; i < 4; ++i) {
                g[i * 4 + i] = Complex{ 1, 0 };
            }
            return g;
        }

        // Apply a 2x2 matrix to the PHYSICAL leg (mode 0) of a site tensor:
        //   t'[s'][rest] = sum_s m[s*2 + s'] t[s][rest]  (absorb_leg convention).
        // A convenience wrapper naming mode 0 explicitly.
        void apply_physical(GraphSite& s, const std::vector<Complex>& m)
        {
            absorb_leg(s.t, site_dims(s), 0, m);
        }

        // ─── the Vidal simple-update edge gate ────────────────────────────────
        //
        // Apply the two-site gate G (4x4, [out*4 + in]) across edge e = (u, v) and
        // re-truncate the bond to chi. Returns the RELATIVE truncation error of
        // this gate = discarded / (kept + discarded).
        //
        // Vidal simple update: absorb the mean-field ENVIRONMENT (every incident
        // lambda EXCEPT e's) into u and v, contract them across e (weighted by the
        // shared lambda) into a two-site tensor Theta, gate the two physical legs,
        // SVD-truncate the cut back to chi, split into new Gamma_u / Gamma_v, then
        // divide the environment back out (regularized inverse). On a tree this is
        // exact; on a loopy graph it is the standard BP/mean-field approximation.
        double apply_edge_gate(
            wz::core::graph::SharedEdgeGraph<GraphSite, GraphBond>& graph,
            NodeHandle u, NodeHandle v, GraphBond& bond,
            const std::vector<Complex>& gate, uint32_t chi,
            wz::engine::cognition::qstate::Svd& svd_out,
            wz::engine::cognition::qstate::SvdScratch& svd_scratch)
        {
            GraphSite& su = node_data(graph, u);
            GraphSite& sv = node_data(graph, v);
            const Incidences iu = incidences_of(graph, u);
            const Incidences iv = incidences_of(graph, v);

            // leg index of e within each node (its neighbor's position). Parallel
            // edges are canonicalized away, so the neighbor handle identifies e.
            std::size_t ku = 0;
            for (std::size_t k = 0; k < iu.size(); ++k) {
                if (iu.neighbor(k) == v) {
                    ku = k;
                    break;
                }
            }
            std::size_t kv = 0;
            for (std::size_t k = 0; k < iv.size(); ++k) {
                if (iv.neighbor(k) == u) {
                    kv = k;
                    break;
                }
            }

            const std::vector<double>& lam = bond.lambda;
            const uint32_t D = static_cast<uint32_t>(lam.size());  // shared bond

            // ── step 2: absorb the environment (every incident lambda except e).
            // Work on copies so the divide-back reads the SAME neighbor lambdas.
            std::vector<Complex> wu = su.t;
            std::vector<Complex> wv = sv.t;
            std::vector<uint32_t> du = site_dims(su);
            std::vector<uint32_t> dv = site_dims(sv);
            for (std::size_t k = 0; k < iu.size(); ++k) {
                if (k == ku) {
                    continue;
                }
                scale_leg(wu, du, 1 + k, iu.bond(k)->lambda);
            }
            for (std::size_t k = 0; k < iv.size(); ++k) {
                if (k == kv) {
                    continue;
                }
                scale_leg(wv, dv, 1 + k, iv.bond(k)->lambda);
            }

            // ── step 3: contract u and v across e into Theta.
            // Group each node's legs into (row/col outer index over its OTHER legs
            // + physical) x (shared bond leg). For u the mode order is
            // [2, bond_0..bond_{deg_u-1}]; e is at mode (1 + ku). Flatten u into
            // Au[ru][m], ru ranging over u's physical + non-e bonds (in mode
            // order, e removed), m the shared bond index. Likewise Bv[m][cv].
            //
            // ru layout: iterate modes 0.. of u in order, skipping the e leg; the
            // combined extent is size(wu)/D. cv layout: same for v.
            const std::size_t ru_extent = wu.size() / D;
            const std::size_t cv_extent = wv.size() / D;

            // Strides of the e leg within each flat tensor (product of dims after
            // that mode). wu layout is [2, bond_0, ...]; e is mode (1 + ku).
            std::size_t u_e_inner = 1;
            for (std::size_t m = 1 + ku + 1; m < du.size(); ++m) {
                u_e_inner *= du[m];
            }
            const std::size_t u_e_stride = u_e_inner;         // step per e index
            const std::size_t u_e_block = static_cast<std::size_t>(D) * u_e_inner;

            std::size_t v_e_inner = 1;
            for (std::size_t m = 1 + kv + 1; m < dv.size(); ++m) {
                v_e_inner *= dv[m];
            }
            const std::size_t v_e_stride = v_e_inner;
            const std::size_t v_e_block = static_cast<std::size_t>(D) * v_e_inner;

            // Flatten wu -> Au[ru * D + m] by pulling the e index out to the
            // trailing position; ru enumerates the (outer, inner) pair around e.
            std::vector<Complex> au(ru_extent * D, Complex{ 0, 0 });
            {
                const std::size_t u_outer = wu.size() / u_e_block;
                std::size_t ru = 0;
                for (std::size_t o = 0; o < u_outer; ++o) {
                    const std::size_t obase = o * u_e_block;
                    for (std::size_t inr = 0; inr < u_e_inner; ++inr) {
                        for (uint32_t m = 0; m < D; ++m) {
                            au[ru * D + m] =
                                wu[obase + m * u_e_stride + inr];
                        }
                        ++ru;
                    }
                }
            }
            // Flatten wv -> Bv[m * cv_extent + cv], the shared bond leading.
            std::vector<Complex> bv(static_cast<std::size_t>(D) * cv_extent,
                Complex{ 0, 0 });
            {
                const std::size_t v_outer = wv.size() / v_e_block;
                std::size_t cv = 0;
                for (std::size_t o = 0; o < v_outer; ++o) {
                    const std::size_t obase = o * v_e_block;
                    for (std::size_t inr = 0; inr < v_e_inner; ++inr) {
                        for (uint32_t m = 0; m < D; ++m) {
                            bv[static_cast<std::size_t>(m) * cv_extent + cv] =
                                wv[obase + m * v_e_stride + inr];
                        }
                        ++cv;
                    }
                }
            }

            // Theta[ru][cv] = sum_m Au[ru][m] * lam[m] * Bv[m][cv].
            std::vector<Complex> theta(ru_extent * cv_extent, Complex{ 0, 0 });
            for (std::size_t ru = 0; ru < ru_extent; ++ru) {
                for (uint32_t m = 0; m < D; ++m) {
                    const Complex w = au[ru * D + m] * Complex{ lam[m], 0 };
                    if (w == Complex{ 0, 0 }) {
                        continue;
                    }
                    const std::size_t brow = static_cast<std::size_t>(m) * cv_extent;
                    for (std::size_t cv = 0; cv < cv_extent; ++cv) {
                        theta[ru * cv_extent + cv] += w * bv[brow + cv];
                    }
                }
            }

            // ── step 4: apply the 4x4 gate to the two physical legs of Theta.
            // physical_u is the mode-0 index of u -> the high part of ru; physical
            // _v is the mode-0 index of v -> the high part of cv. Extract those
            // sub-extents so the gate mixes only the 2x2 physical block.
            const std::size_t ru_phys_inner = ru_extent / 2;  // per physical_u
            const std::size_t cv_phys_inner = cv_extent / 2;  // per physical_v
            {
                std::vector<Complex> gated(theta.size(), Complex{ 0, 0 });
                for (uint32_t s0 = 0; s0 < 2; ++s0) {
                    for (std::size_t ri = 0; ri < ru_phys_inner; ++ri) {
                        const std::size_t ru = s0 * ru_phys_inner + ri;
                        for (uint32_t s1 = 0; s1 < 2; ++s1) {
                            for (std::size_t ci = 0; ci < cv_phys_inner; ++ci) {
                                const std::size_t cv = s1 * cv_phys_inner + ci;
                                const Complex in =
                                    theta[ru * cv_extent + cv];
                                if (in == Complex{ 0, 0 }) {
                                    continue;
                                }
                                for (uint32_t s0p = 0; s0p < 2; ++s0p) {
                                    const std::size_t rup =
                                        s0p * ru_phys_inner + ri;
                                    for (uint32_t s1p = 0; s1p < 2; ++s1p) {
                                        const std::size_t cvp =
                                            s1p * cv_phys_inner + ci;
                                        gated[rup * cv_extent + cvp] +=
                                            gate[(s0p * 2 + s1p) * 4
                                                + (s0 * 2 + s1)]
                                            * in;
                                    }
                                }
                            }
                        }
                    }
                }
                theta.swap(gated);
            }

            // ── step 5: SVD-truncate the cut to chi.
            wz::engine::cognition::qstate::Svd& d = svd_out;
            wz::engine::cognition::qstate::svd(
                theta, static_cast<uint32_t>(ru_extent),
                static_cast<uint32_t>(cv_extent), chi, d, svd_scratch);
            const uint32_t k = d.rank;

            double kept = 0.0;
            for (uint32_t r = 0; r < k; ++r) {
                kept += d.s[r] * d.s[r];
            }
            const double discarded = d.discarded_weight;
            const double rel_error =
                (kept + discarded) > 0.0 ? discarded / (kept + discarded) : 0.0;

            // New lambda' = singular values, normalized so sum(lambda'^2) = 1
            // (keep rel_error, computed BEFORE this normalization).
            std::vector<double> lam_new(k, 0.0);
            double s_norm = 0.0;
            for (uint32_t r = 0; r < k; ++r) {
                s_norm += d.s[r] * d.s[r];
            }
            const double s_inv = s_norm > 0.0 ? 1.0 / std::sqrt(s_norm) : 0.0;
            for (uint32_t r = 0; r < k; ++r) {
                lam_new[r] = d.s[r] * s_inv;
            }

            // ── step 6: split back. Undo the ru/cv flattening, reinserting the
            // new e index (dim k) at the leg position it occupied (1 + ku for u,
            // 1 + kv for v). The forward flatten pulled u's e leg to the TRAILING
            // position of au (au[ru * D + m], ru = outer * u_e_inner + inr around
            // the e leg) and v's e leg to the LEADING position of bv (bv[m *
            // cv_extent + cv], cv = outer * v_e_inner + inr). We invert each map
            // exactly: the same (outer, inr) decomposition of ru / cv places the
            // element back at [outer, e = m, inr] in the reshaped tensor.
            //
            // U (ru_extent x k) -> Gamma_u (env still absorbed; divided out below).
            std::vector<uint32_t> du_new = du;
            du_new[1 + ku] = k;
            std::vector<Complex> wu_new(ru_extent * k, Complex{ 0, 0 });
            {
                const std::size_t u_new_block =
                    static_cast<std::size_t>(k) * u_e_inner;  // [e=k, inner]
                for (std::size_t ru = 0; ru < ru_extent; ++ru) {
                    const std::size_t o = ru / u_e_inner;    // outer index
                    const std::size_t inr = ru % u_e_inner;  // inner index
                    const std::size_t obase = o * u_new_block;
                    for (uint32_t m = 0; m < k; ++m) {
                        wu_new[obase + m * u_e_inner + inr] = d.u[ru * k + m];
                    }
                }
            }
            // Vh (k x cv_extent) -> Gamma_v directly. The Vidal split is Theta =
            // Gamma_u * diag(lambda') * Gamma_v with Gamma_u = env_u^{-1} U and
            // Gamma_v = Vh env_v^{-1}; lambda' (the normalized singular values)
            // carries the Schmidt weight on the edge, so neither Gamma absorbs s.
            // (An overall ||s|| scale is dropped with lambda's normalization; the
            // lambda-gauge marginal divides by trace, so that factor is immaterial.)
            std::vector<uint32_t> dv_new = dv;
            dv_new[1 + kv] = k;
            std::vector<Complex> wv_new(static_cast<std::size_t>(k) * cv_extent,
                Complex{ 0, 0 });
            {
                const std::size_t v_new_block =
                    static_cast<std::size_t>(k) * v_e_inner;  // [e=k, inner]
                for (std::size_t cv = 0; cv < cv_extent; ++cv) {
                    const std::size_t o = cv / v_e_inner;    // outer index
                    const std::size_t inr = cv % v_e_inner;  // inner index
                    const std::size_t obase = o * v_new_block;
                    for (uint32_t m = 0; m < k; ++m) {
                        wv_new[obase + m * v_e_inner + inr] =
                            d.vh[static_cast<std::size_t>(m) * cv_extent + cv];
                    }
                }
            }

            // ── step 7: remove the environment absorbed in step 2, restoring the
            // bare Vidal Gammas (regularized inverse 1/max(lambda_nbr, floor)
            // along each same neighbor leg). The floor bounds the inverse on near-
            // zero Schmidt values that would otherwise blow up.
            for (std::size_t kk = 0; kk < iu.size(); ++kk) {
                if (kk == ku) {
                    continue;
                }
                const std::vector<double>& nbr = iu.bond(kk)->lambda;
                std::vector<double> inv(nbr.size(), 0.0);
                for (std::size_t i = 0; i < nbr.size(); ++i) {
                    inv[i] = 1.0 / std::max(nbr[i], kLambdaInvFloor);
                }
                scale_leg(wu_new, du_new, 1 + kk, inv);
            }
            for (std::size_t kk = 0; kk < iv.size(); ++kk) {
                if (kk == kv) {
                    continue;
                }
                const std::vector<double>& nbr = iv.bond(kk)->lambda;
                std::vector<double> inv(nbr.size(), 0.0);
                for (std::size_t i = 0; i < nbr.size(); ++i) {
                    inv[i] = 1.0 / std::max(nbr[i], kLambdaInvFloor);
                }
                scale_leg(wv_new, dv_new, 1 + kk, inv);
            }

            // ── step 8: commit the new tensors, bond dims, and shared lambda.
            su.t = std::move(wu_new);
            su.bond_dim[ku] = k;
            sv.t = std::move(wv_new);
            sv.bond_dim[kv] = k;
            bond.lambda = std::move(lam_new);

            return rel_error;
        }

        // ─── the lambda-gauge marginal (readout) ──────────────────────────────
        //
        // Agent u's <sigma_z> from the Vidal/BP reduced density matrix: absorb
        // diag(lambda^2) along every incident bond leg, then contract the tensor
        // against its own conjugate over all bond legs, leaving the physical leg
        // -> a 2x2 diagonal rho -> (rho00 - rho11) / (rho00 + rho11). Exact on a
        // tree.
        double marginal_z(
            wz::core::graph::SharedEdgeGraph<GraphSite, GraphBond>& graph,
            NodeHandle u)
        {
            const GraphSite& s = node_data(graph, u);
            const Incidences inc = incidences_of(graph, u);

            std::vector<Complex> w = s.t;  // ket working copy
            const std::vector<uint32_t> dims = site_dims(s);
            for (std::size_t k = 0; k < inc.size(); ++k) {
                const std::vector<double>& lam = inc.bond(k)->lambda;
                std::vector<double> lam2(lam.size(), 0.0);
                for (std::size_t i = 0; i < lam.size(); ++i) {
                    lam2[i] = lam[i] * lam[i];
                }
                scale_leg(w, dims, 1 + k, lam2);
            }

            // w now carries diag(lambda^2) on every bond leg; contract everything
            // except the physical leg against conj(s.t), keeping s free.
            std::size_t tail = 1;  // product of bond dims
            for (uint32_t d : s.bond_dim) {
                tail *= d;
            }
            Complex rho[2] = { Complex{ 0, 0 }, Complex{ 0, 0 } };
            for (uint32_t phys = 0; phys < 2; ++phys) {
                const std::size_t base = static_cast<std::size_t>(phys) * tail;
                Complex acc{ 0, 0 };
                for (std::size_t j = 0; j < tail; ++j) {
                    acc += w[base + j] * std::conj(s.t[base + j]);
                }
                rho[phys] = acc;
            }
            const double p0 = rho[0].real();
            const double p1 = rho[1].real();
            const double total = p0 + p1;
            return total > 0.0 ? (p0 - p1) / total : 0.0;
        }

        // Restore Vidal canonical form (the lambda-gauge the marginal readout and
        // the environment approximation both assume) by re-SVDing every bond from
        // the current tensors via an IDENTITY two-site update. Each identity update
        // contracts Gamma_u * diag(lambda) * Gamma_v, SVDs it, and re-splits --
        // refreshing that bond's lambda to the true Schmidt spectrum and re-
        // orthonormalizing both endpoint Gammas, WITHOUT changing the physical
        // state. One sweep is exact on a tree; a few passes settle the BP gauge on
        // loops. The bond is re-capped at `chi` so a loop's identity update cannot
        // regrow a bond past the cap the coupling gate set; the (tiny) truncation
        // error of a canonicalization pass is NOT telemetered -- only the coupling
        // gate's is.
        void canonicalize(GraphTn& g, uint32_t chi, uint32_t passes)
        {
            const std::vector<Complex> id = identity_gate();
            const uint32_t n = node_count(g.graph);
            // Each pass is a full forward-then-backward sweep. Sweeping both
            // directions propagates the gauge from one end of a tree to the other
            // and back, so a single pass canonicalizes a chain/tree exactly; the
            // extra passes settle the gauge on loops (where no single sweep order
            // is exact).
            for (uint32_t pass = 0; pass < passes; ++pass) {
                for (NodeHandle u = 0; u < n; ++u) {
                    for_each_neighbor(g.graph, u, [&](NodeHandle v, GraphBond& e) {
                        if (v <= u) {
                            return;  // each undirected edge once, low -> high
                        }
                        apply_edge_gate(g.graph, u, v, e, id, chi, g.svd_out, g.svd_scratch);
                    });
                }
                for (NodeHandle uu = n; uu-- > 0;) {
                    for_each_neighbor(g.graph, uu, [&](NodeHandle v, GraphBond& e) {
                        if (v >= uu) {
                            return;  // each undirected edge once, high -> low
                        }
                        apply_edge_gate(g.graph, uu, v, e, id, chi, g.svd_out, g.svd_scratch);
                    });
                }
            }
        }
    }  // namespace

    GraphTn make_graph_tn(
        uint32_t agent_count, const std::vector<ExactBond>& bonds, uint32_t chi)
    {
        const CanonicalBonds canon = canonicalize_bonds(agent_count, bonds);

        wz::core::graph::SharedEdgeGraphBuilder<GraphSite, GraphBond> b;

        // One node per agent: a product |+> state (unpolarized physical marginal),
        // all bond legs dim 1. The degree/bond_dim are filled once we know each
        // node's degree; for now start every tensor as the 2-vector (1, 1) over
        // the trivial bonds, i.e. equal superposition on the physical leg. We must
        // know the degree BEFORE sizing the tensor, so count incident bonds first.
        std::vector<uint32_t> deg(agent_count, 0);
        for (const ExactBond& e : canon.bonds) {
            ++deg[e.a];
            ++deg[e.b];
        }
        const double amp = 1.0 / std::sqrt(2.0);  // |+> = (|0> + |1>)/sqrt2
        for (uint32_t i = 0; i < agent_count; ++i) {
            GraphSite s;
            s.degree = deg[i];
            s.bond_dim.assign(deg[i], 1u);  // every bond starts dim 1
            // 2 * prod(1) == 2 entries: the physical |+> on trivial bonds.
            s.t = { Complex{ amp, 0 }, Complex{ amp, 0 } };
            add_node(b, std::move(s));
        }

        for (const ExactBond& e : canon.bonds) {
            add_edge(b, e.a, e.b, GraphBond{ .j = e.j, .lambda = { 1.0 } });
        }

        return GraphTn{
            .graph = build(std::move(b)),
            .goal_field = std::vector<double>(agent_count, 0.0),
            .chi = chi,
            .clamp = std::vector<int8_t>(agent_count, -1),
            .last_truncation_error = 0.0,
        };
    }

    void set_goals(GraphTn& g, const std::vector<Goal>& goals)
    {
        std::fill(g.goal_field.begin(), g.goal_field.end(), 0.0);
        for (const Goal& goal : goals) {
            if (goal.agent < g.goal_field.size()) {
                g.goal_field[goal.agent] += goal.field;
            }
        }
    }

    void relax_step(GraphTn& g, double gamma, double dtau)
    {
        // 2nd-order symmetric (Strang) split, matching exact_group / ttn: half a
        // step of the single-site fields, a full step of the two-site couplings,
        // then the other half of the fields. The symmetric ordering cancels the
        // leading commutator (local error O(dtau^3)).
        g.last_truncation_error = 0.0;
        const uint32_t n = node_count(g.graph);

        auto half_fields = [&]() {
            for (NodeHandle u = 0; u < n; ++u) {
                const double h =
                    u < g.goal_field.size() ? g.goal_field[u] : 0.0;
                apply_physical(
                    node_data(g.graph, u), field_matrix(gamma, h, dtau * 0.5));
            }
        };

        half_fields();

        // Full coupling step: one imaginary-time ZZ per edge via simple update.
        // Iterate every undirected edge once. We reach each edge from its lower-
        // handle endpoint so it is visited exactly once, and pass the shared slot.
        for (NodeHandle u = 0; u < n; ++u) {
            for_each_neighbor(g.graph, u, [&](NodeHandle v, GraphBond& e) {
                if (v <= u) {
                    return;  // visit each undirected edge once (from lower u)
                }
                g.last_truncation_error += apply_edge_gate(
                    g.graph, u, v, e, zz_gate(e.j, dtau), g.chi,
                    g.svd_out, g.svd_scratch);
            });
        }

        half_fields();

        // Re-apply every held collapse BEFORE the canonicalization below, not
        // after. Both the field gates and the coupling gates mix a projected
        // physical leg back off its latch, so the clamp has to be restored each
        // step -- and the sweep that follows is exactly what propagates the
        // projection outward into the neighbours' environments. Folding the
        // re-projection into a sweep that was going to run anyway is what makes
        // holding a clamp on this backend affordable at all; doing it as a
        // separate collapse() per substep would re-canonicalize the whole graph
        // once per clamped agent per substep.
        for (uint32_t u = 0; u < n && u < g.clamp.size(); ++u) {
            if (g.clamp[u] < 0) {
                continue;
            }
            GraphSite& s = node_data(g.graph, u);
            std::size_t tail = 1;
            for (uint32_t d : s.bond_dim) {
                tail *= d;
            }
            const std::size_t zero_phys = g.clamp[u] != 0 ? 0u : 1u;
            const std::size_t base = zero_phys * tail;
            for (std::size_t j = 0; j < tail; ++j) {
                s.t[base + j] = Complex{ 0, 0 };
            }
        }

        // The trailing single-site field gates are non-unitary rotations on the
        // physical legs; they perturb each node's tensor OUT of Vidal canonical
        // form (the lambdas no longer are the exact Schmidt spectra of the current
        // state). The lambda-gauge marginal is only valid in canonical form, so we
        // restore it: a canonicalization sweep re-SVDs every bond from the current
        // tensors (an identity two-site update -- it refreshes each lambda and re-
        // orthonormalizes both endpoint Gammas without changing the physical
        // state). Exact on a tree in one sweep; a few passes settle the BP gauge on
        // loops. No coupling is applied, so it adds no truncation-error telemetry.
        // Two passes on anything with loops. Dropping to one measures 1.65x faster
        // and moves the BP error only in the 4th decimal (afm ring-6 + goal, chi=2
        // at gamma 1.5: 0.14312 -> 0.14352), so it is a real option -- but this
        // backend is about to carry production decisions and the cost is already
        // affordable, so the fidelity stays.
        canonicalize(g, g.chi, /*passes=*/n > 2 ? 2u : 1u);
    }

    void relax(GraphTn& g, double gamma, double dtau, uint32_t iterations)
    {
        for (uint32_t i = 0; i < iterations; ++i) {
            relax_step(g, gamma, dtau);
        }
    }

    double decision_z(const GraphTn& g, uint32_t agent)
    {
        // marginal_z needs a mutable graph handle (for_each_neighbor is non-const
        // on the shared-edge graph); the read itself mutates nothing.
        return marginal_z(
            const_cast<wz::core::graph::SharedEdgeGraph<GraphSite, GraphBond>&>(
                g.graph),
            agent);
    }

    std::vector<double> decisions(const GraphTn& g)
    {
        const uint32_t n = node_count(g.graph);
        std::vector<double> z(n, 0.0);
        for (uint32_t i = 0; i < n; ++i) {
            z[i] = decision_z(g, i);
        }
        return z;
    }

    void collapse(GraphTn& g, uint32_t agent, bool bit)
    {
        // Project the physical leg onto `bit`: zero the opposite physical
        // component of the tensor.
        if (agent >= node_count(g.graph)) {
            return;
        }
        if (agent < g.clamp.size()) {
            g.clamp[agent] = bit ? 1 : 0;   // held across every later relax_step
        }
        GraphSite& s = node_data(g.graph, agent);
        std::size_t tail = 1;
        for (uint32_t d : s.bond_dim) {
            tail *= d;
        }
        const std::size_t zero_phys = bit ? 0u : 1u;  // component to zero out
        const std::size_t base = zero_phys * tail;
        for (std::size_t j = 0; j < tail; ++j) {
            s.t[base + j] = Complex{ 0, 0 };
        }

        // The projection perturbs only this node's LOCAL tensor, but a coupled
        // neighbour's decision must CONDITION on the committed outcome (the
        // entangled-cat mechanism: collapsing one arm drags its partner). A local
        // lambda-gauge marginal cannot see a neighbour's projection on its own --
        // so re-canonicalize, which re-SVDs the incident bonds and propagates the
        // projection outward into the neighbours' environments (their lambdas and
        // Gammas), exactly as conditioning should. A few passes carry it across the
        // graph; the readout marginal then reflects the committed outcome.
        //
        // A CONSTANT number of passes, not node_count. Each pass is already a full
        // forward-then-backward sweep over every edge, which canonicalize's own
        // contract says is exact on a tree and settles the gauge on loops in a few
        // -- so scaling the passes with the group size was quadratic work for no
        // extra conditioning. It matters: the store re-projects every latched agent
        // on every tick, so an O(n) collapse is O(n^2) per tick, and holding a
        // clamp through relaxation (the uniform collapse contract) would multiply
        // that by the substep count.
        canonicalize(g, g.chi, kConditioningPasses);
    }

    bool measure_in_basis(
        GraphTn& g, uint32_t agent, double theta, qstate::Rng& rng)
    {
        if (agent >= node_count(g.graph)) {
            return false;
        }
        // Rotate the measurement axis onto z with a single-site R_y(-theta) on the
        // physical leg (unitary -> no bond growth, no truncation), matching the
        // exact and TTN backends exactly.
        const double c = std::cos(theta * 0.5);
        const double s = std::sin(theta * 0.5);
        // absorb_leg contracts as t'[s'] = sum_s m[s*2 + s'] t[s], i.e. the matrix
        // is indexed [in*2 + out] -- the TRANSPOSE of the [out*2 + in] convention
        // qstate::apply_1q uses. R_y(-theta) is {c, s; -s, c} in [out*2 + in], so
        // it goes in here as {c, -s; s, c}.
        const std::vector<Complex> ry = {
            Complex{ c, 0 }, Complex{ -s, 0 },
            Complex{ s, 0 }, Complex{ c, 0 },
        };
        apply_physical(node_data(g.graph, agent), ry);
        // The rotation perturbs the lambda gauge the marginal readout assumes, so
        // restore it before sampling -- otherwise the Born probability is read off
        // a state that is not in canonical form.
        canonicalize(g, g.chi, kConditioningPasses);

        // Born-sample from the lambda-gauge marginal, which is already conditioned
        // on any previously collapsed agents. P(|1>) = (1 - <sigma_z>) / 2.
        const double z = marginal_z(g.graph, agent);
        const double p1 = 0.5 * (1.0 - z);
        const bool bit = rng.next_unit() < p1;
        collapse(g, agent, bit);   // projects, clamps, and conditions the graph
        return bit;
    }
}
