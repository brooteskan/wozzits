#include <engine/cognition/coordination.h>

namespace wz::cognition
{
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
}
