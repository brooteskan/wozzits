#pragma once

// cognition/cognition_clock.h
//
// The sim-time driver above the Coordination seam: it maps an agent's self-paced
// wakes (irregular sim-time stamps) onto the continuous-time imaginary-time
// relaxation of its coordination state. This is the math side of the self-paced
// "cognition.tick" -- the agent wakes when IT decides to, supplies the current
// sim-time, and the clock derives how far to relax (dtau) and where in the
// exploration->commit sweep it is (Gamma) from the elapsed time, NOT from a fixed
// per-frame step. So the decision trajectory is a function of sim-time, not of how
// the wakes happened to land: an agent that sleeps for a second and wakes once
// reaches the same mental state as one that woke twenty times along the way
// (a long step is substepped under max_substep so it relaxes identically).
//
// Sim-time, not wall-clock: pausing the world pauses deliberation, slow-mo slows
// it, and the world clock is the single master that render frames, audio, and
// cognition all stamp against.

#include <cognition/coordination.h>

#include <cstdint>

namespace wz::engine::cognition
{
    // A self-contained continuous-time annealing schedule + relaxation clock. The
    // Gamma sweep is linear over [started_at, started_at + anneal_seconds] in sim-
    // time; imaginary-time advances at relax_rate per sim-second.
    struct CognitionClock
    {
        double gamma_start = 2.0;     // exploratory transverse field at sweep start
        double gamma_end = 0.0;       // committed transverse field at sweep end
        double anneal_seconds = 0.0;  // sim-seconds the Gamma sweep spans (0 -> hold gamma_end)
        double relax_rate = 1.0;      // imaginary-time per sim-second: dtau = relax_rate * elapsed
        double max_substep = 0.05;    // cap on a single imaginary-time step (0 -> no substepping)
        uint32_t max_substeps = 1024; // hard cap on substeps per tick (bounds WORK on a
                                      // pathologically long sleep -- max_substep bounds
                                      // the per-step ERROR, not the count; 0 -> uncapped)

        double started_at = 0.0;      // sim-time the sweep was zeroed (self.start)
        double last_tick = 0.0;       // sim-time of the previous wake
        bool started = false;         // has the origin been stamped yet?
    };

    // How many substeps a tick spanning `dtau_total` imaginary-time takes: ceil(
    // dtau_total / max_substep) so no step exceeds max_substep, clamped to max_substeps
    // so the WORK stays bounded on a pathologically long sleep. max_substep <= 0 (no
    // substepping) or dtau_total <= max_substep -> 1; max_substeps == 0 -> uncapped.
    // Pure and header-visible so it can be unit-tested directly.
    uint32_t clock_substep_count(double dtau_total, double max_substep, uint32_t max_substeps);

    // Zero the clock at sim-time `now`: this is the self.start moment, before the
    // first relaxation. After this, gamma_at_time(now) == gamma_start.
    void start(CognitionClock& clock, double now);

    // The annealing field Gamma at sim-time `now`, linearly interpolated over the
    // sweep and clamped to [gamma_end, gamma_start] outside it.
    double gamma_at_time(const CognitionClock& clock, double now);

    // Advance `c` to sim-time `now`. Relaxes by dtau = relax_rate * (now - last_tick),
    // substepped so no single imaginary-time step exceeds max_substep, sampling Gamma
    // at each substep's sim-time so the sweep is honored at sub-tick resolution; then
    // stamps last_tick = now. A first tick (or one with now <= last_tick) only zeroes
    // the clock / no-ops. Returns the total imaginary-time actually applied.
    double tick(Coordination& c, CognitionClock& clock, double now);
}
