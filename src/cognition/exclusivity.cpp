#include <cognition/exclusivity.h>

namespace wz::engine::cognition
{
    void add_one_hot(
        ExactGroup& g,
        const AgentLayout& layout,
        uint32_t agent,
        double strength)
    {
        if (agent >= layout.dispositions.size()) {
            return;
        }
        const uint32_t k = layout.dispositions[agent];
        const double j = -strength / 2.0;                    // antiferro pair cost
        const double h = strength * (static_cast<double>(k) - 2.0) / 2.0;  // inactive bias

        // These bonds are appended AFTER make_exact_group has run its
        // out-of-range endpoint guard, so they have to guard themselves: a
        // layout wider than the group it is being applied to would otherwise
        // push a bond naming a qubit the joint register does not have, and
        // relax_step applies each bond's ZZ unchecked -- an endpoint in
        // [qubits, 64) silently degenerates the bond into a phantom
        // single-site field, and >= 64 is shift UB.
        const uint32_t qubits = g.joint.qubits;

        for (uint32_t i = 0; i < k; ++i) {
            const uint32_t qi = qubit_of(layout, agent, i);
            if (qi >= qubits) {
                continue;
            }
            if (qi < g.goal_field.size()) {
                g.goal_field[qi] += h;
            }
            for (uint32_t jj = i + 1; jj < k; ++jj) {
                const uint32_t qj = qubit_of(layout, agent, jj);
                if (qj >= qubits) {
                    continue;
                }
                g.bonds.push_back(ExactBond{ .a = qi, .b = qj, .j = j });
            }
        }
    }
}
