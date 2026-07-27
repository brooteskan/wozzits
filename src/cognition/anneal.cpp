#include <cognition/anneal.h>

#include <cognition/mean_field.h>

namespace wz::engine::cognition
{
    double gamma_at(const AnnealSchedule& s, uint32_t step)
    {
        if (s.steps <= 1) {
            return s.gamma_end;   // a single step IS the end of the sweep
        }
        // The step-count driver's whole job: turn a step index into a phase in
        // [0, 1], then defer to the ramp cognition_clock owns.
        return gamma_at_phase(
            s.gamma_start, s.gamma_end,
            static_cast<double>(step) / static_cast<double>(s.steps - 1));
    }

    // The exact / TTN / graph / loopy backends go through the header's template;
    // mean-field needs its messages refreshed first, so it keeps an overload.
    void anneal(MeanFieldNetwork& net, const AnnealSchedule& s)
    {
        refresh_messages(net);
        for (uint32_t i = 0; i < s.steps; ++i) {
            relax_step(net, gamma_at(s, i), s.dtau);
        }
    }
}
