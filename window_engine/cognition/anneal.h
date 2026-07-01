#pragma once

// cognition/anneal.h
//
// Quantum-annealing schedule for the cognition backends: the transverse field
// Gamma is ramped DOWN over a relaxation, taking agents from exploratory
// superposition (high Gamma -- undecided, uncorrelated) to a committed decision
// (low Gamma -- ordered: a definite or entangled joint choice). This is the
// "deliberate, then commit" dynamic, and the mechanism by which the group
// relaxes toward the joint-optimal decision (the ground state of the final
// Hamiltonian).
//
// The schedule is backend-agnostic: it just steps a backend's relax at the
// scheduled Gamma. Both the chi=1 mean-field and the exact joint-state backends
// are supported.

#include <cognition/exact_group.h>
#include <cognition/mean_field.h>

#include <cstdint>

namespace wz::engine::cognition
{
    struct AnnealSchedule
    {
        double gamma_start = 2.0;   // exploratory (high transverse field)
        double gamma_end = 0.0;     // committed (low transverse field)
        double dtau = 0.05;         // imaginary-time step size
        uint32_t steps = 200;
    };

    // Linearly interpolated Gamma at step `step` in [0, steps).
    double gamma_at(const AnnealSchedule& s, uint32_t step);

    // Run the schedule on a backend (relax_step at the scheduled Gamma each step).
    void anneal(MeanFieldNetwork& net, const AnnealSchedule& s);
    void anneal(ExactGroup& g, const AnnealSchedule& s);
}
