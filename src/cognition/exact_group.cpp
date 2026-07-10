#include <cognition/exact_group.h>

#include <algorithm>

namespace wz::engine::cognition
{
    ExactGroup make_exact_group(
        uint32_t agent_count, std::vector<ExactBond> bonds)
    {
        // Drop any bond with an out-of-range endpoint. relax_step applies each bond's
        // ZZ UNCHECKED (unlike the goal loop, which guards q < goal_field.size()), so an
        // out-of-range endpoint would address a non-existent qubit: its bit reads a
        // constant 0, silently degenerating the bond into a phantom single-site field,
        // and an index >= 64 is shift-UB. The store's create() canonicalizes bonds
        // first, but make_exact_group is public, so it guards itself.
        bonds.erase(
            std::remove_if(
                bonds.begin(), bonds.end(),
                [agent_count](const ExactBond& b) {
                    return b.a >= agent_count || b.b >= agent_count;
                }),
            bonds.end());
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
        //
        // 2nd-order symmetric (Strang) split: half a step of the single-site
        // fields, a full step of the two-site couplings, then the other half of
        // the fields. The symmetric ordering cancels the leading commutator, so
        // the local error is O(dtau^3) (vs O(dtau^2) for the plain field-then-
        // coupling split) at the cost of one extra cheap single-site half-sweep.
        const uint32_t n = g.joint.qubits;
        for (uint32_t q = 0; q < n; ++q) {
            const double h = q < g.goal_field.size() ? g.goal_field[q] : 0.0;
            qstate::apply_imag_time_field(g.joint, q, gamma, h, dtau * 0.5);
        }
        for (const ExactBond& b : g.bonds) {
            qstate::apply_imag_time_zz(g.joint, b.a, b.b, b.j, dtau);
        }
        for (uint32_t q = 0; q < n; ++q) {
            const double h = q < g.goal_field.size() ? g.goal_field[q] : 0.0;
            qstate::apply_imag_time_field(g.joint, q, gamma, h, dtau * 0.5);
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
