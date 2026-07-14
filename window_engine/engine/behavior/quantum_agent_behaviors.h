#pragma once

// engine/behavior/quantum_agent_behaviors.h
//
// Built-in "quantum_agent" decider MODULE: the actuator-facing front end of the
// cognition stack (wz::engine::cognition). It gives an NPC a deliberating wave function
// whose committed decision other behaviors can act on. It is the consumer of the
// two engine seams below it:
//   * self.start    -> build the agent's coordination state in the engine-side
//                      AgentCognitionStore (from this binding's config) and zero
//                      its deliberation clock;
//   * cognition.tick -> relax one self-paced step + try to commit, then schedule
//                       the next wake. NOT a per-frame tick -- the agent thinks on
//                       its own clock (think_interval), so a hundred NPCs do not
//                       each run a function every frame.
//
// A module (not a function behavior) on purpose: function behaviors run every
// frame regardless of subscription, whereas modules are event-gated. The agent's
// wave function (a std::vector-bearing register) cannot live in the behavior's
// trivially-copyable instance state, so the store owns it and this binding keeps
// only a POD handle + the cached decision (QuantumAgentState).
//
// First cut: a LONE single-disposition agent (one binary choice biased by a goal),
// the common NPC case. Coordinated groups (couplings between agents) compose at the
// store level and are a follow-up in the authoring surface.

#include <engine/behavior/behavior_plugin_abi.h>
#include <cognition/agent_cognition.h>

#include <cstdint>

namespace wz::engine::behavior
{
    inline constexpr const char* kQuantumAgentModule = "quantum_agent";

    // The process-wide owner of every quantum_agent's wave function. Exposed so the
    // host-side read/write seams (behavior_plugin_adapter) can push goal re-biases
    // and re-arm an agent by the handle its POD state carries.
    wz::engine::cognition::AgentCognitionStore& quantum_agent_store();

    // Config keys (all Number) read once on self.start:
    //   goal            - longitudinal goal bias on the disposition. > 0 favors the
    //                     |0> outcome (decision_z > 0), < 0 favors |1>; magnitude is
    //                     the goal's importance. Default 0 (undecided).
    //   gamma_start     - exploratory transverse field at the start of the anneal.
    //                     Default 2.0.
    //   anneal_seconds  - sim-seconds the exploration->commit Gamma sweep spans.
    //                     Default 4.0.
    //   relax_rate      - imaginary-time relaxed per sim-second. Default 1.0.
    //   confidence      - commit when the leading outcome's probability >= this.
    //                     Default 0.8.
    //   decoherence     - environmental-pressure collapse rate (per sim-second);
    //                     forces a snap decision even while undecided. Default 0.
    //   think_interval  - sim-seconds between self-paced wakes. Default 0.25.
    inline constexpr const char* kQuantumAgentGoalKey = "goal";
    inline constexpr const char* kQuantumAgentGammaStartKey = "gamma_start";
    inline constexpr const char* kQuantumAgentAnnealSecondsKey = "anneal_seconds";
    inline constexpr const char* kQuantumAgentRelaxRateKey = "relax_rate";
    inline constexpr const char* kQuantumAgentConfidenceKey = "confidence";
    inline constexpr const char* kQuantumAgentDecoherenceKey = "decoherence";
    inline constexpr const char* kQuantumAgentThinkIntervalKey = "think_interval";

    // Second coupled decision (qubit 1): a SECOND longitudinal goal + a bond that
    // couples it to qubit 0, so the two dispositions entangle and resolve together.
    //   posture_goal - bias on qubit 1 (> 0 favors |0>, < 0 favors |1>). Default 0.
    //   coupling     - bond j between qubit 0 and 1: > 0 ferromagnetic (they lean
    //                  to AGREE in sign), < 0 anti (disagree), 0 independent. When
    //                  the two goals fight the coupling the agent is frustrated and
    //                  WAVERS before committing. Default 0 (no second decision).
    inline constexpr const char* kQuantumAgentPostureGoalKey = "posture_goal";
    inline constexpr const char* kQuantumAgentCouplingKey = "coupling";

    // Number of coupled decisions (qubits) this agent deliberates. Default 2
    // (pursue + posture); an actuator that drives extra decisions (e.g. a "when to
    // reconsider" meta-qubit) via the write seam raises it. Clamped to
    // [1, kQuantumAgentMaxDecisions]. The `coupling` bond stays on qubits 0<->1;
    // extra qubits start uncoupled and their goals come from the write seam.
    inline constexpr const char* kQuantumAgentDecisionsKey = "decisions";

    // Star coupling: if non-zero, bond qubit 0 to EVERY other qubit (0<->i for
    // i in 1..decisions-1) with this strength -- makes qubit 0 a hub, i.e. a GROUP
    // / command node whose decision entangles a whole squad of member qubits (each
    // member reads its own qubit of this shared agent). > 0 ferromagnetic (members
    // correlate with the hub). Use INSTEAD of `coupling` (which is the 0<->1 pair).
    inline constexpr const char* kQuantumAgentStarCouplingKey = "star_coupling";

    // Coordination BACKEND selector (chi). 0 = exact joint state (genuine
    // entanglement, small groups; the default, and what every current NPC uses);
    // 1 = loopy BP (any topology incl. CYCLIC villages, scales linearly, but a
    // product-state approximation with NO entanglement); >= 2 = chi-truncated TTN
    // chain (larger entangled groups; bonds MUST form the nearest-neighbour chain,
    // so pair it with chain_coupling, not ring/star). Default 0. An out-of-topology
    // spec (e.g. chi>=2 with a ring) fails to build -- create() returns invalid and
    // the module logs it.
    inline constexpr const char* kQuantumAgentChiKey = "chi";

    // Nearest-neighbour topology for the linear-scaling backends -- use ONE of these
    // INSTEAD of coupling/star for a many-membered group:
    //   chain_coupling - bond (i, i+1) into an OPEN chain (the shape a chi>=2 TTN
    //                    requires). Default 0.
    //   ring_coupling  - the chain PLUS the closing bond (n-1, 0): a CYCLE. An odd
    //                    anti-ferromagnetic ring is FRUSTRATED (no 2-coloring) -- the
    //                    thing a tree cannot show; run it on chi=1 (a chi>=2 TTN needs
    //                    an open chain, so a ring fails to build). The closing bond is
    //                    skipped below 3 qubits (a 2-ring would double the lone bond).
    //                    Default 0.
    inline constexpr const char* kQuantumAgentChainCouplingKey = "chain_coupling";
    inline constexpr const char* kQuantumAgentRingCouplingKey = "ring_coupling";

    // LEARNING: number of MEMORY qubits held outside the coordination (never
    // measured, so their learned bias accumulates across commits / rearms /
    // reshapes). 0 = no memory. An actuator reinforces them via wz_agent_reward
    // and reads them back via wz_agent_memory to bias its goals toward what paid
    // off.
    inline constexpr const char* kQuantumAgentMemoryKey = "memory";

    // An authored MIND IR (schema "wozzits.mind.ir.v0"; see mind_ir.h): a JSON graph
    // of decision qubits, their goal biases, and the couplings (bonds) between them,
    // plus backend/anneal/commit/memory. When present it SUPERSEDES every scalar key
    // above -- the mind is authored as an arbitrary graph rather than a star/chain/
    // ring family. A present-but-malformed mind_ir fails loudly (no silent fallback).
    inline constexpr const char* kQuantumAgentMindIrKey = "mind_ir";

    // Cap on coupled decisions a single agent exposes (keeps the POD state fixed-
    // size + trivially copyable). Bump if a richer NPC needs more qubits. 5 lets
    // the tank carry a 5th qubit (a BLINK/teleport disposition) alongside
    // pursue/posture/reconsider/fire.
    inline constexpr uint32_t kQuantumAgentMaxDecisions = 5;

    // Per-binding instance state (POD; trivially copyable, preserved across scene
    // rebuilds). Public so an actuator / read surface -- and tests -- can read the
    // agent's committed decisions and live marginals without reaching into the
    // engine-internal store. The module caches every qubit here on each think.
    struct QuantumAgentState
    {
        uint64_t handle = 0;           // AgentCognitionStore handle (0 = not built)
        float think_interval = 0.25f;  // self-paced cadence (sim-seconds)
        uint8_t started = 0;           // agent created on self.start?
        uint8_t agent_count = 1;       // number of coupled decisions built
        // Per-qubit cache. [i] = decision i; -1 committed = deliberating, else
        // 0 (|0>) / 1 (|1>); marginal = live <sigma_z> in [-1, 1].
        int8_t committed[kQuantumAgentMaxDecisions] = { -1, -1, -1, -1, -1 };
        float marginal[kQuantumAgentMaxDecisions] =
            { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    };

    uint8_t register_quantum_agent_behaviors(WzBehaviorPluginApi* api);
}
