#include <cognition/coordination.h>

#include <algorithm>

namespace wz::engine::cognition
{
    namespace
    {
        // Reset then sum the goals into a per-agent longitudinal field vector --
        // the shared shape of ExactGroup::goal_field / TtnChain::goal_field.
        void assign_goal_field(
            std::vector<double>& field, const std::vector<Goal>& goals)
        {
            std::fill(field.begin(), field.end(), 0.0);
            for (const Goal& goal : goals) {
                if (goal.agent < field.size()) {
                    field[goal.agent] += goal.field;
                }
            }
        }
    }

    // Per-backend set_goals so the std::visit below resolves for every variant
    // alternative. ExactGroup already has one (exact_group.h); the TTN chain
    // mirrors it; mean-field goals are not wired yet so it is a no-op.
    void set_goals(TtnChain& t, const std::vector<Goal>& goals)
    {
        assign_goal_field(t.goal_field, goals);
    }

    void set_goals(MeanFieldNetwork&, const std::vector<Goal>&)
    {
        // Mean-field (chi == 1) goal support is a follow-up; create() rejects it,
        // so a live agent is never this backend.
    }

    void relax(Coordination& c, double gamma, double dtau, uint32_t iterations)
    {
        std::visit(
            [&](auto& backend) { relax(backend, gamma, dtau, iterations); }, c);
    }

    double decision_z(Coordination& c, uint32_t agent)
    {
        return std::visit(
            [&](auto& backend) { return decision_z(backend, agent); }, c);
    }

    void set_goals(Coordination& c, const std::vector<Goal>& goals)
    {
        std::visit([&](auto& backend) { set_goals(backend, goals); }, c);
    }
}
