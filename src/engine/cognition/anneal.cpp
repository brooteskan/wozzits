#include <engine/cognition/anneal.h>

namespace wz::cognition
{
    double gamma_at(const AnnealSchedule& s, uint32_t step)
    {
        if (s.steps <= 1) {
            return s.gamma_end;
        }
        const double t =
            static_cast<double>(step) / static_cast<double>(s.steps - 1);
        return s.gamma_start + (s.gamma_end - s.gamma_start) * t;
    }

    void anneal(MeanFieldNetwork& net, const AnnealSchedule& s)
    {
        refresh_messages(net);
        for (uint32_t i = 0; i < s.steps; ++i) {
            relax_step(net, gamma_at(s, i), s.dtau);
        }
    }

    void anneal(ExactGroup& g, const AnnealSchedule& s)
    {
        for (uint32_t i = 0; i < s.steps; ++i) {
            relax_step(g, gamma_at(s, i), s.dtau);
        }
    }
}
