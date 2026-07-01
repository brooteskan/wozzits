#include <cognition/exact_group.h>

#include <algorithm>

namespace wz::engine::cognition
{
    ExactGroup make_exact_group(
        uint32_t agent_count, std::vector<ExactBond> bonds)
    {
        return ExactGroup{
            .joint = qstate::uniform(agent_count),
            .bonds = std::move(bonds),
            .goal_field = std::vector<double>(agent_count, 0.0),
        };
    }

    void set_goals(ExactGroup& g, const std::vector<Goal>& goals)
    {
        std::fill(g.goal_field.begin(), g.goal_field.end(), 0.0);
        for (const Goal& goal : goals) {
            if (goal.agent < g.goal_field.size()) {
                g.goal_field[goal.agent] += goal.field;
            }
        }
    }

    void relax_step(ExactGroup& g, double gamma, double dtau)
    {
        // Imaginary-time Trotter step for
        //   H = -gamma sum_i sigma_x^i - sum_i h_i sigma_z^i
        //       - sum_bonds j sigma_z sigma_z,
        // where h_i is agent i's goal field.
        const uint32_t n = g.joint.qubits;
        for (uint32_t q = 0; q < n; ++q) {
            const double h = q < g.goal_field.size() ? g.goal_field[q] : 0.0;
            qstate::apply_imag_time_field(g.joint, q, gamma, h, dtau);
        }
        for (const ExactBond& b : g.bonds) {
            qstate::apply_imag_time_zz(g.joint, b.a, b.b, b.j, dtau);
        }
    }

    void relax(ExactGroup& g, double gamma, double dtau, uint32_t iterations)
    {
        for (uint32_t i = 0; i < iterations; ++i) {
            relax_step(g, gamma, dtau);
        }
    }

    double decision_z(const ExactGroup& g, uint32_t agent)
    {
        return qstate::expectation_z(g.joint, agent);
    }

    std::vector<double> decisions(const ExactGroup& g)
    {
        std::vector<double> z(g.joint.qubits, 0.0);
        for (uint32_t i = 0; i < g.joint.qubits; ++i) {
            z[i] = qstate::expectation_z(g.joint, i);
        }
        return z;
    }

    void collapse(ExactGroup& g, uint32_t agent, bool bit)
    {
        qstate::project(g.joint, agent, bit);
    }

    double connected_correlation(const ExactGroup& g, uint32_t a, uint32_t b)
    {
        return qstate::expectation_zz(g.joint, a, b)
            - qstate::expectation_z(g.joint, a)
                * qstate::expectation_z(g.joint, b);
    }
}
