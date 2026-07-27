#include <cognition/coordination.h>

#include <algorithm>
#include <type_traits>

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
    // alternative. ExactGroup already has one (exact_group.h), as does GraphTn
    // (graph_tn.cpp); the TTN chain mirrors them.
    void set_goals(TtnChain& t, const std::vector<Goal>& goals)
    {
        assign_goal_field(t.goal_field, goals);
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

    double truncation_error(const Coordination& c)
    {
        // Only the chi-capped backends truncate; loopy BP (chi=1, a product state
        // with nothing to truncate) and the exact joint state (chi=inf) never drop
        // weight, so both report 0.
        return std::visit(
            [](const auto& backend) -> double {
                using B = std::decay_t<decltype(backend)>;
                if constexpr (std::is_same_v<B, TtnChain>
                    || std::is_same_v<B, GraphTn>)
                {
                    return backend.last_truncation_error;
                } else {
                    return 0.0;
                }
            },
            c);
    }

    std::vector<double> decisions(Coordination& c)
    {
        return std::visit(
            [&](auto& backend) { return decisions(backend); }, c);
    }

    void collapse(Coordination& c, uint32_t agent, bool bit)
    {
        std::visit([&](auto& backend) { collapse(backend, agent, bit); }, c);
    }

    void set_goals(Coordination& c, const std::vector<Goal>& goals)
    {
        std::visit([&](auto& backend) { set_goals(backend, goals); }, c);
    }

    bool measure_in_basis(
        Coordination& c, uint32_t agent, double theta, qstate::Rng& rng)
    {
        // Each backend supplies its own measure_in_basis overload (entangling for
        // exact/TTN, local/classical for the product-state backends); the uint32_t
        // agent converts to the mean-field NodeHandle as it does for collapse.
        return std::visit(
            [&](auto& backend) {
                return measure_in_basis(backend, agent, theta, rng);
            },
            c);
    }
}
