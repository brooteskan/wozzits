#include <cognition/exclusivity.h>

namespace wz::engine::cognition
{
    void add_one_hot(
        std::vector<ExactBond>& bonds,
        std::vector<Goal>& goals,
        const AgentLayout& layout,
        uint32_t agent,
        double strength,
        uint32_t qubit_count)
    {
        if (strength == 0.0 || agent >= layout.dispositions.size()) {
            return;
        }
        const uint32_t k = layout.dispositions[agent];
        const double j = -strength / 2.0;                    // antiferro pair cost
        const double h = strength * (static_cast<double>(k) - 2.0) / 2.0;  // inactive bias

        // Every term is bounded against the group it is being applied to. These
        // land AFTER make_exact_group's endpoint guard has run (or, on the spec
        // path, alongside authored bonds that were validated separately), and
        // relax_step applies each bond's ZZ unchecked -- an endpoint in
        // [qubit_count, 64) silently degenerates the bond into a phantom
        // single-site field, and >= 64 is shift UB. A layout can legitimately be
        // wider than the group addressing it, so this is not a can't-happen.
        for (uint32_t i = 0; i < k; ++i) {
            const uint32_t qi = qubit_of(layout, agent, i);
            if (qi >= qubit_count) {
                continue;
            }
            goals.push_back(Goal{ .agent = qi, .field = h });
            for (uint32_t jj = i + 1; jj < k; ++jj) {
                const uint32_t qj = qubit_of(layout, agent, jj);
                if (qj >= qubit_count) {
                    continue;
                }
                bonds.push_back(ExactBond{ .a = qi, .b = qj, .j = j });
            }
        }
    }

    void add_one_hot(
        ExactGroup& g,
        const AgentLayout& layout,
        uint32_t agent,
        double strength)
    {
        // One implementation: expand into spec-level terms bounded by this group's
        // own qubit count, then merge. set_goals sums onto the dense goal field,
        // and this is additive, so the two forms agree term for term.
        std::vector<ExactBond> bonds;
        std::vector<Goal> goals;
        add_one_hot(bonds, goals, layout, agent, strength, g.joint.qubits);

        for (const ExactBond& b : bonds) {
            g.bonds.push_back(b);
        }
        for (const Goal& goal : goals) {
            if (goal.agent < g.goal_field.size()) {
                g.goal_field[goal.agent] += goal.field;
            }
        }
    }
}
