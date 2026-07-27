#pragma once

// cognition/anneal.h
//
// The STEP-COUNT driver of the anneal schedule: ramp the transverse field Gamma
// down over a fixed number of relaxation steps, taking agents from exploratory
// superposition (high Gamma -- undecided, uncorrelated) to a committed decision
// (low Gamma -- ordered: a definite or entangled joint choice).
//
// WHICH DRIVER IS WHICH. There is ONE schedule, and it lives in
// cognition_clock.h as gamma_at_phase. Two things drive it:
//
//   * cognition_clock::tick -- the SIM-TIME driver, and the one the ENGINE runs.
//     An agent wakes when it decides to, supplies the current sim-time, and the
//     clock derives how far to relax and where in the sweep it is. Pausing the
//     world pauses deliberation. This is the production path; if you are asking
//     "how does the engine anneal?", the answer is there, not here.
//   * anneal() below -- the STEP-COUNT driver, for tests and offline tools that
//     want a deterministic number of relaxations rather than a sim-time span.
//
// They differ only in how they compute phase. Both call gamma_at_phase, so the
// schedule cannot drift between them -- which it could when each carried its own
// copy of the interpolation (and a third copy lived in loopy_bp_tests).

#include <cognition/cognition_clock.h>
#include <cognition/mean_field.h>   // only the MeanFieldNetwork overload needs it

#include <cstdint>

namespace wz::engine::cognition
{
    // A fixed-step sweep of the shared Gamma ramp.
    //
    // gamma_end defaults to 0.0 -- the fully classical endpoint -- matching
    // CognitionClock's library default. Note that the AUTHORING default the
    // engine ships is different (kQuantumAgentDefaultGammaEnd = 0.5, applied at
    // the behavior layer): the library defaults stay at the undriven classical
    // baseline so the cognition tests get an exact endpoint, and the behavior
    // layer opts into a residual field. Do not "fix" one to match the other.
    struct AnnealSchedule
    {
        double gamma_start = 2.0;   // exploratory (high transverse field)
        double gamma_end = 0.0;     // committed (low transverse field)
        double dtau = 0.05;         // imaginary-time step size
        uint32_t steps = 200;
    };

    // Gamma at step `step` of `steps`. The step-count driver's whole job: turn a
    // step index into a phase, then defer to the shared ramp.
    double gamma_at(const AnnealSchedule& s, uint32_t step);

    // Run the schedule on ANY backend exposing relax_step(backend, gamma, dtau)
    // -- ExactGroup, TtnChain, GraphTn, LoopyBpGroup. It used to have exactly two
    // hand-written overloads (mean-field and exact), which is why loopy_bp_tests
    // had to write its own copy to drive a third backend.
    template <class Backend>
    void anneal(Backend& backend, const AnnealSchedule& s)
    {
        for (uint32_t i = 0; i < s.steps; ++i) {
            relax_step(backend, gamma_at(s, i), s.dtau);
        }
    }

    // Mean-field needs its messages refreshed before the sweep, so it keeps a
    // non-template overload (which wins overload resolution over the template).
    void anneal(MeanFieldNetwork& net, const AnnealSchedule& s);
}
