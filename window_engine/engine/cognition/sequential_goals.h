#pragma once

// engine/cognition/sequential_goals.h
//
// Sequential / staged objectives. The agent's CURRENT phase lives in a memory
// "phase register" (a small qstate register), so the goal stack is memory
// amplitude rather than a separate state machine -- it persists like any memory
// and stays in the substrate. The active goal is whichever stage matches the
// current phase; completing an objective ADVANCES the phase (a memory
// transition), which switches the live goal and so re-points the decision at the
// next objective. Do-A-then-B-then-C falls out of advancing the phase.
//
// This first cut treats the phase as a definite value (the register's dominant
// basis state) and advances it deterministically; a fully coherent
// phase-controlled goal (the phase superposed, gating the decision through
// entanglement) is a richer follow-up.

#include <engine/cognition/exact_group.h>  // Goal
#include <engine/qstate/qstate.h>

#include <cstdint>
#include <vector>

namespace wz::cognition
{
    // One stage of a plan: while the current phase == `phase`, the agent pursues
    // goal `field` on decision qubit `qubit`.
    struct Stage
    {
        uint64_t phase = 0;
        uint32_t qubit = 0;
        double field = 0.0;
    };

    // A phase register over `qubits` qubits (up to 2^qubits phases), starting at
    // phase 0.
    wz::qstate::Register make_phase_register(uint32_t qubits);

    // The current phase = the register's dominant basis state.
    uint64_t current_phase(const wz::qstate::Register& phase_register);

    // Advance to the next phase (a definite transition to |phase + 1>, wrapping).
    // Call when the current objective completes.
    void advance_phase(wz::qstate::Register& phase_register);

    // The goals active in `phase` (the stages whose phase matches), ready to feed
    // to set_goals() on the decision group.
    std::vector<Goal> active_goals(
        const std::vector<Stage>& stages, uint64_t phase);
}
