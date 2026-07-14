#include <cognition/ttn.h>

#include <graph/shared_edge_polytree.h>

#include <cmath>
#include <utility>

namespace wz::engine::cognition
{
    namespace
    {
        using qstate::Complex;
        using wz::core::graph::add_edge;
        using wz::core::graph::add_node;
        using wz::core::graph::build;
        using wz::core::graph::node_count;
        using wz::core::graph::node_data;
        using wz::core::graph::NodeHandle;
        using wz::core::graph::SharedEdgePolytreeBuilder;

        // Single-site imaginary-time gate e^{-H dtau}, H = -gamma sigma_x - h sigma_z.
        // H^2 = E^2 I (E = sqrt(h^2+gamma^2)), so e^{-H dtau} =
        //   cosh(E dtau) I + (sinh(E dtau)/E)(gamma sigma_x + h sigma_z).
        std::vector<Complex> site_gate(double gamma, double h, double dtau)
        {
            const double e = std::sqrt(h * h + gamma * gamma);
            if (e <= 0) {
                return { Complex{ 1, 0 }, Complex{ 0, 0 },
                    Complex{ 0, 0 }, Complex{ 1, 0 } };
            }
            const double ch = std::cosh(e * dtau);
            const double she = std::sinh(e * dtau) / e;
            return {
                Complex{ ch + she * h, 0 }, Complex{ she * gamma, 0 },
                Complex{ she * gamma, 0 }, Complex{ ch - she * h, 0 },
            };
        }

        // Two-site imaginary-time coupling gate e^{-H dtau}, H = -j sigma_z sigma_z:
        // diagonal, e^{+j dtau} when the two spins agree, e^{-j dtau} when they differ.
        std::vector<Complex> coupling_gate(double j, double dtau)
        {
            const double agree = std::exp(j * dtau);
            const double differ = std::exp(-j * dtau);
            std::vector<Complex> g(16, Complex{ 0, 0 });
            g[0 * 4 + 0] = Complex{ agree, 0 };   // 00
            g[1 * 4 + 1] = Complex{ differ, 0 };  // 01
            g[2 * 4 + 2] = Complex{ differ, 0 };  // 10
            g[3 * 4 + 3] = Complex{ agree, 0 };   // 11
            return g;
        }

        // Rescale every MPS site to unit Frobenius norm. The imaginary-time gates are
        // NON-unitary (e^{-H dtau} shrinks/grows amplitudes), so without this the
        // network's 2-norm drifts every relax_step -- it grows ~e^{|E0| dtau} per step
        // and, over a long-lived relaxation (a persistent agent), overflows to inf.
        // The marginals are trace-normalized (tree_bp_sigma_z), so an overflowed state
        // reads inf/inf = NaN, try_commit stops firing (NaN compares false), and the
        // agent silently freezes. A per-site rescale is a pure GAUGE: the physical state
        // is the contraction of the sites, so dividing each site by a scalar only
        // rescales the whole state -- which the trace-normalized reads are invariant to.
        // It changes nothing physical; it just keeps the amplitudes in range. (Mirrors
        // qstate's normalize-after-each-imaginary-time-op and graph_tn's lambda renorm.)
        void renormalize_sites(TreeBpNetwork& mps)
        {
            const uint32_t n = node_count(mps);
            for (uint32_t i = 0; i < n; ++i) {
                MpsSite& A = node_data(mps, static_cast<NodeHandle>(i));
                double sumsq = 0.0;
                for (const Complex& c : A.a) {
                    sumsq += c.real() * c.real() + c.imag() * c.imag();
                }
                if (sumsq > 0.0) {
                    const double inv = 1.0 / std::sqrt(sumsq);
                    for (Complex& c : A.a) {
                        c *= inv;
                    }
                }
            }
        }
    }

    TtnChain make_ttn_chain(
        uint32_t n, std::vector<double> coupling, uint32_t chi)
    {
        SharedEdgePolytreeBuilder<MpsSite, BondEnv> b;
        std::vector<NodeHandle> nodes;
        const double r = 1.0 / std::sqrt(2.0);
        for (uint32_t i = 0; i < n; ++i) {
            MpsSite s;
            s.left = 1;
            s.right = 1;
            s.a = { Complex{ r, 0 }, Complex{ r, 0 } };  // |+>
            nodes.push_back(add_node(b, std::move(s)));
        }
        for (uint32_t i = 1; i < n; ++i) {
            add_edge(b, nodes[i - 1], nodes[i], BondEnv{});
        }

        TtnChain g;
        g.mps = std::move(*build(std::move(b)));
        g.coupling = std::move(coupling);
        g.goal_field.assign(n, 0.0);
        g.chi = chi;
        return g;
    }

    void relax_step(TtnChain& g, double gamma, double dtau)
    {
        const uint32_t n = node_count(g.mps);
        g.last_truncation_error = 0.0;
        // 2nd-order symmetric (Strang) split: half a step of the single-site
        // fields, a full step of the two-site couplings, then the other half of
        // the fields. The symmetric ordering makes the local error O(dtau^3)
        // (vs O(dtau^2) for the plain field-then-coupling split) for one extra
        // cheap single-site half-sweep. Truncation happens ONLY on the coupling
        // gates, so last_truncation_error is reset once at the top and then
        // accumulated only across the full-step coupling sweep -- the single-site
        // half-sweeps do not truncate and must not touch it.
        // Single-site fields, first half-step (commute across sites).
        for (uint32_t i = 0; i < n; ++i) {
            apply_one_site_gate(
                node_data(g.mps, i), site_gate(gamma, g.goal_field[i], dtau * 0.5));
        }
        // Nearest-neighbour couplings, full step, each truncated to chi.
        for (uint32_t i = 0; i + 1 < n; ++i) {
            g.last_truncation_error += apply_two_site_gate(
                node_data(g.mps, i),
                node_data(g.mps, i + 1),
                coupling_gate(g.coupling[i], dtau),
                g.chi);
        }
        // Single-site fields, second half-step.
        for (uint32_t i = 0; i < n; ++i) {
            apply_one_site_gate(
                node_data(g.mps, i), site_gate(gamma, g.goal_field[i], dtau * 0.5));
        }
        // The gates above are non-unitary, so rescale the sites back into range or the
        // MPS 2-norm compounds each step and eventually overflows to inf/NaN (see
        // renormalize_sites). Pure gauge -- the trace-normalized marginals are unchanged.
        renormalize_sites(g.mps);
    }

    void relax(TtnChain& g, double gamma, double dtau, uint32_t iterations)
    {
        for (uint32_t i = 0; i < iterations; ++i) {
            relax_step(g, gamma, dtau);
        }
    }

    std::vector<double> decisions(TtnChain& g)
    {
        return tree_bp_sigma_z(g.mps);
    }

    double decision_z(TtnChain& g, uint32_t agent)
    {
        return tree_bp_sigma_z(g.mps)[agent];
    }

    void collapse(TtnChain& g, uint32_t agent, bool bit)
    {
        // Zero the OPPOSITE physical component of the site (layout A[(s*L+l)*R+r]).
        MpsSite& A = node_data(g.mps, static_cast<NodeHandle>(agent));
        const uint32_t s_zero = bit ? 0u : 1u;
        for (uint32_t l = 0; l < A.left; ++l) {
            for (uint32_t r = 0; r < A.right; ++r) {
                A.a[(static_cast<std::size_t>(s_zero) * A.left + l) * A.right + r] =
                    Complex{ 0, 0 };
            }
        }
    }

    bool measure_in_basis(
        TtnChain& g, uint32_t agent, double theta, qstate::Rng& rng)
    {
        if (agent >= node_count(g.mps)) {
            return false;
        }
        // Rotate the measurement axis onto z with a single-site R_y(-theta) gate
        // (unitary -> no bond growth, no truncation; the reads are trace-
        // normalized so no renormalize is needed either).
        const double c = std::cos(theta * 0.5);
        const double s = std::sin(theta * 0.5);
        const std::vector<Complex> u = {
            Complex{ c, 0 }, Complex{ s, 0 },
            Complex{ -s, 0 }, Complex{ c, 0 },
        };
        apply_one_site_gate(node_data(g.mps, static_cast<NodeHandle>(agent)), u);
        // Born-sample from the site's marginal (tree BP gives it CONDITIONED on any
        // already-collapsed sites): P(|1>) = (1 - <sigma_z>) / 2, matching qstate.
        const double z = tree_bp_sigma_z(g.mps)[agent];
        const double p1 = 0.5 * (1.0 - z);
        const bool bit = rng.next_unit() < p1;
        collapse(g, agent, bit);  // project + condition the chain through the bonds
        return bit;
    }
}
