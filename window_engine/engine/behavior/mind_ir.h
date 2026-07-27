#pragma once

// engine/behavior/mind_ir.h
//
// Parse an authored "mind" (schema "wozzits.mind.ir.v0") into a
// cognition::AgentSpec the AgentCognitionStore builds a wave function from. A mind
// IS a graph: decision qubits are NODES, their pairwise couplings (bonds) are
// EDGES, and each qubit carries a longitudinal goal bias. This is the data-driven
// counterpart to the quantum_agent behavior's scalar star/chain/ring config -- it
// lets a mind be authored as an ARBITRARY graph rather than picked from a family,
// and is the engine seam a graph-based mind editor compiles to (parallel to the
// statechart IR the statechart editor compiles to).
//
// The JSON shape (all fields but `qubits` optional):
//   {
//     "schema": "wozzits.mind.ir.v0",
//     "qubits": 3,                                    // decision qubits (>= 1)
//     "goals":  [ {"q": 0, "field": 0.4}, ... ],      // per-qubit bias (+ |0>, - |1>)
//     "bonds":  [ {"a": 0, "b": 1, "j": -0.8}, ... ], // couplings (+ ferro, - anti)
//
//     // MULTI-DISPOSITION agents (additive; absent = one qubit per agent):
//     "dispositions": [3, 1],                         // agent 0 owns 3, agent 1 owns 1
//     "one_hot":      [2.0, 0],                       // agent 0's 3 are EXCLUSIVE
//     // With a layout, goals and bonds may address (agent, disposition) instead
//     // of a flat qubit -- the two forms may be mixed freely:
//     //   {"agent": 0, "disposition": 1, "field": -0.4}
//     //   {"a_agent": 0, "a_disposition": 2, "b_agent": 1, "b_disposition": 0,
//     //    "j": 0.5}
//     // `qubits` may be omitted with a layout; if given it must equal the sum.

//     "chi":    0,                                    // backend: 0 exact/1 loopy/>=2 TTN
//     "memory": 0,                                    // learning-register qubits
//     "clock":  { "gamma_start": 2.0, "gamma_end": 0.5,
//                 "anneal_seconds": 4.0, "relax_rate": 1.0 },
//     "commit": { "confidence": 0.8, "decoherence": 0.0 }
//   }
//
// gamma_end defaults to the shared quantum_agent authoring value
// (kQuantumAgentDefaultGammaEnd). It is the RESIDUAL transverse field: 0 leaves
// the Hamiltonian purely diagonal at commit, i.e. no superposition or
// entanglement survives the sweep. Set "gamma_end": 0.0 explicitly for a mind
// that should land fully classical -- and see the note in
// quantum_agent_behaviors.h for how a live field caps `confidence`.

#include <cognition/agent_cognition.h>

#include <string>

namespace wz::engine::behavior
{
    // Fill `out` (agent_count, goals, bonds, chi, memory_qubits, clock, commit) from
    // the mind IR JSON. Leaves `out.seed` untouched -- the quantum_agent behavior
    // derives a per-instance seed. Returns false + sets `error` on malformed JSON, a
    // missing/zero `qubits`, or a goal/bond naming an out-of-range qubit. Backend
    // selection is left to AgentCognitionStore::create, which fails closed on an
    // unbuildable spec.
    bool parse_mind(
        const std::string& json_text,
        cognition::AgentSpec& out,
        std::string& error);
}
