#include <cognition/exclusivity.h>

namespace wz::engine::cognition
{
    void add_one_hot(
        ExactGroup& g,
        const AgentLayout& layout,
        uint32_t agent,
        double strength)
    {
        const uint32_t k = layout.dispositions[agent];
        const double j = -strength / 2.0;                    // antiferro pair cost
        const double h = strength * (static_cast<double>(k) - 2.0) / 2.0;  // inactive bias

        for (uint32_t i = 0; i < k; ++i) {
            const uint32_t qi = qubit_of(layout, agent, i);
            if (qi < g.goal_field.size()) {
                g.goal_field[qi] += h;
            }
            for (uint32_t jj = i + 1; jj < k; ++jj) {
                g.bonds.push_back(
                    ExactBond{ .a = qi, .b = qubit_of(layout, agent, jj),
                        .j = j });
            }
        }
    }
}
