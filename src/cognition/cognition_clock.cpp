#include <cognition/cognition_clock.h>

#include <algorithm>
#include <cmath>

namespace wz::engine::cognition
{
    uint32_t clock_substep_count(double dtau_total, double max_substep, uint32_t max_substeps)
    {
        uint32_t substeps = 1;
        if (max_substep > 0.0 && dtau_total > max_substep) {
            substeps = static_cast<uint32_t>(std::ceil(dtau_total / max_substep));
        }
        // Hard cap the WORK: max_substep bounds the per-step error, not the count, so a
        // long sleep can otherwise demand thousands of relax() calls. Clamping still
        // consumes the full elapsed span (dt_sim = elapsed / substeps) -- just coarser.
        if (max_substeps > 0 && substeps > max_substeps) {
            substeps = max_substeps;
        }
        return substeps;
    }

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
        // at sub-tick resolution. The count is capped (max_substeps) so a pathologically
        // long wake does bounded WORK; the full elapsed span is still consumed.
        const uint32_t substeps =
            clock_substep_count(dtau_total, clock.max_substep, clock.max_substeps);

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
