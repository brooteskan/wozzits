#include <engine/cognition/exact_group.h>

#include <algorithm>

namespace wz::cognition
{
    ExactGroup make_exact_group(
        uint32_t agent_count, std::vector<ExactBond> bonds)
    {
        return ExactGroup{
            .joint = wz::qstate::uniform(agent_count),
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
            wz::qstate::apply_imag_time_field(g.joint, q, gamma, h, dtau);
        }
        for (const ExactBond& b : g.bonds) {
            wz::qstate::apply_imag_time_zz(g.joint, b.a, b.b, b.j, dtau);
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
        return wz::qstate::expectation_z(g.joint, agent);
    }

    double connected_correlation(const ExactGroup& g, uint32_t a, uint32_t b)
    {
        return wz::qstate::expectation_zz(g.joint, a, b)
            - wz::qstate::expectation_z(g.joint, a)
                * wz::qstate::expectation_z(g.joint, b);
    }
}
