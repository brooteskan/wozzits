#include <engine/cognition/cognition_clock.h>

#include <algorithm>
#include <cmath>

namespace wz::cognition
{
    void start(CognitionClock& clock, double now)
    {
        clock.started_at = now;
        clock.last_tick = now;
        clock.started = true;
    }

    double gamma_at_time(const CognitionClock& clock, double now)
    {
        if (clock.anneal_seconds <= 0.0) {
            return clock.gamma_end;
        }
        const double phase =
            (now - clock.started_at) / clock.anneal_seconds;
        const double clamped = std::clamp(phase, 0.0, 1.0);
        return clock.gamma_start +
            (clock.gamma_end - clock.gamma_start) * clamped;
    }

    double tick(Coordination& c, CognitionClock& clock, double now)
    {
        if (!clock.started) {
            // First contact only stamps the origin; there is no elapsed time to
            // relax across yet.
            start(clock, now);
            return 0.0;
        }

        const double elapsed = now - clock.last_tick;
        if (elapsed <= 0.0) {
            return 0.0;  // wakes that don't advance sim-time do no work.
        }

        const double dtau_total = clock.relax_rate * elapsed;

        // Substep so a long sleep relaxes the same as many short wakes: split the
        // elapsed sim-time into chunks whose imaginary-time step is <= max_substep,
        // and sample Gamma at each chunk's midpoint so the anneal sweep is honored
        // at sub-tick resolution.
        uint32_t substeps = 1;
        if (clock.max_substep > 0.0 && dtau_total > clock.max_substep) {
            substeps = static_cast<uint32_t>(
                std::ceil(dtau_total / clock.max_substep));
        }

        const double dt_sim = elapsed / static_cast<double>(substeps);
        const double dtau = clock.relax_rate * dt_sim;
        for (uint32_t i = 0; i < substeps; ++i) {
            const double t_mid =
                clock.last_tick + (static_cast<double>(i) + 0.5) * dt_sim;
            relax(c, gamma_at_time(clock, t_mid), dtau, /*iterations=*/1);
        }

        clock.last_tick = now;
        return dtau_total;
    }
}
