#include <cognition/ttn.h>

#include <graph/shared_edge_polytree.h>

#include <cmath>
#include <utility>

namespace wz::engine::cognition
{
    namespace
    {
        using wz::engine::cognition::qstate::Complex;
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
        // Single-site fields (commute across sites).
        for (uint32_t i = 0; i < n; ++i) {
            apply_one_site_gate(
                node_data(g.mps, i), site_gate(gamma, g.goal_field[i], dtau));
        }
        // Nearest-neighbour couplings, each truncated to chi.
        for (uint32_t i = 0; i + 1 < n; ++i) {
            apply_two_site_gate(
                node_data(g.mps, i),
                node_data(g.mps, i + 1),
                coupling_gate(g.coupling[i], dtau),
                g.chi);
        }
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
}
