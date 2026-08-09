#include <cognition/loopy_bp.h>

#include <cognition/group_topology.h>

#include <algorithm>
#include <utility>

namespace wz::engine::cognition
{
    using wz::core::graph::add_edge;
    using wz::core::graph::add_node;
    using wz::core::graph::build;
    using wz::core::graph::for_each_neighbor;
    using wz::core::graph::for_each_node;
    using wz::core::graph::node_count;
    using wz::core::graph::node_data;
    using wz::core::graph::NodeHandle;
    using wz::core::graph::SharedEdgeGraphBuilder;

    LoopyBpGroup make_loopy_bp_group(
        uint32_t agent_count,
        const std::vector<ExactBond>& bonds,
        double damping)
    {
        // Canonicalize to girth>=3 via the shared primitive -- the SAME
        // normalization the store applies at its build boundary, so a direct
        // caller (tests) gets identical clean input: self-bonds dropped (a self
        // sigma_z sigma_z is an inert constant), parallel edges on a pair summed,
        // cancelled (zero-j) pairs dropped, bonds emitted a<b. The BP graph must be
        // simple -- parallel edges would duplicate a bond's field and a self-bond
        // is meaningless -- so this is a precondition, not just tidiness.
        const CanonicalBonds canon = canonicalize_bonds(agent_count, bonds);

        SharedEdgeGraphBuilder<qstate::Register, LoopyBond> b;
        for (uint32_t i = 0; i < agent_count; ++i) {
            add_node(b, qstate::uniform(1));
        }
        for (const ExactBond& e : canon.bonds) {
            add_edge(b, e.a, e.b, LoopyBond{ .j = e.j });
        }

        LoopyBpGroup g{
            .graph = build(std::move(b)),
            .goal_field = std::vector<double>(agent_count, 0.0),
            .z = std::vector<double>(agent_count, 0.0),
            .clamped = std::vector<bool>(agent_count, false),
            .damping = damping,
        };
        return g;
    }

    void set_goals(LoopyBpGroup& g, const std::vector<Goal>& goals)
    {
        std::fill(g.goal_field.begin(), g.goal_field.end(), 0.0);
        for (const Goal& goal : goals) {
            if (goal.agent < g.goal_field.size()) {
                g.goal_field[goal.agent] += goal.field;
            }
        }
    }

    void relax_step(LoopyBpGroup& g, double gamma, double dtau)
    {
        const uint32_t n = node_count(g.graph);

        // 1. Effective longitudinal field on each agent from the CURRENT damped
        //    broadcasts (Jacobi: this step reads the previous step's z for every
        //    agent, so compute ALL of them into scratch before relaxing any
        //    register). A clamped neighbor contributes its pinned z, so it acts
        //    as a fixed field on its neighbors.
        std::vector<double> h_eff(n, 0.0);
        for_each_node(g.graph, [&](NodeHandle u) {
            double h = g.goal_field[u];
            for_each_neighbor(g.graph, u, [&](NodeHandle nbr, LoopyBond& e) {
                h += e.j * g.z[nbr];
            });
            h_eff[u] = h;
        });

        // 2. Relax each UNCLAMPED agent toward its ground state under (gamma,
        //    h_eff). Clamped agents are NOT relaxed -- their register stays
        //    projected. Single-qubit agents: the coupled qubit is qubit 0.
        for_each_node(g.graph, [&](NodeHandle u) {
            if (g.clamped[u]) {
                return;
            }
            qstate::apply_imag_time_field(
                node_data(g.graph, u), 0u, gamma, h_eff[u], dtau);
        });

        // 3. Update the broadcasts with damping. Clamped agents keep their
        //    pinned z; unclamped agents mix the previous broadcast with the
        //    freshly relaxed marginal.
        const double lambda = g.damping;
        for_each_node(g.graph, [&](NodeHandle u) {
            if (g.clamped[u]) {
                return;  // pinned in collapse(); left untouched here
            }
            const double marginal =
                qstate::expectation_z(node_data(g.graph, u), 0u);
            g.z[u] = (1.0 - lambda) * g.z[u] + lambda * marginal;
        });
    }

    void relax(LoopyBpGroup& g, double gamma, double dtau, uint32_t iterations)
    {
        // No reset of z: the persistent broadcasts ARE the warm-start.
        for (uint32_t i = 0; i < iterations; ++i) {
            relax_step(g, gamma, dtau);
        }
    }

    double decision_z(const LoopyBpGroup& g, uint32_t agent)
    {
        return qstate::expectation_z(node_data(g.graph, agent), 0u);
    }

    std::vector<double> decisions(const LoopyBpGroup& g)
    {
        const uint32_t n = node_count(g.graph);
        std::vector<double> z(n, 0.0);
        for_each_node(g.graph, [&](NodeHandle u) {
            z[u] = qstate::expectation_z(node_data(g.graph, u), 0u);
        });
        return z;
    }

    void collapse(LoopyBpGroup& g, uint32_t agent, bool bit)
    {
        if (agent >= node_count(g.graph)) {
            return;
        }
        qstate::project(node_data(g.graph, agent), 0u, bit);
        g.clamped[agent] = true;
        // Pin the broadcast so neighbors feel the committed decision as a fixed
        // field: |1> (bit == true) -> <sigma_z> = -1, |0> -> +1.
        g.z[agent] = bit ? -1.0 : +1.0;
    }

    bool measure_in_basis(
        LoopyBpGroup& g, uint32_t agent, double theta, qstate::Rng& rng)
    {
        if (agent >= node_count(g.graph)) {
            return false;
        }
        // Rotated LOCAL measurement of the agent's single-qubit register; measure()
        // leaves it a z-eigenstate matching the outcome. Then clamp + pin the
        // broadcast so neighbors feel the committed value on the next relax --
        // CLASSICAL conditioning only (a product state has no entanglement to
        // back-act through), which is exactly why chi = 1 stays at the Bell floor.
        const bool bit =
            qstate::measure_in_basis(node_data(g.graph, agent), 0u, theta, rng);
        g.clamped[agent] = true;
        g.z[agent] = bit ? -1.0 : +1.0;
        return bit;
    }
}
