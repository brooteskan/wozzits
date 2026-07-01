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

#include <cstdint>

namespace wz::engine::behavior
{
    inline constexpr const char* kQuantumAgentModule = "quantum_agent";

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

    // Per-binding instance state (POD; trivially copyable, preserved across scene
    // rebuilds). Public so an actuator / read surface -- and tests -- can read the
    // agent's committed decision and live marginal without reaching into the
    // engine-internal store. The module caches both here on each think.
    struct QuantumAgentState
    {
        uint64_t handle = 0;           // AgentCognitionStore handle (0 = not built)
        float marginal = 0.0f;         // last think()'s <sigma_z> in [-1, 1]
        float think_interval = 0.25f;  // self-paced cadence (sim-seconds)
        int8_t committed = -1;         // -1 deliberating, 0 = |0>, 1 = |1>
        uint8_t started = 0;           // agent created on self.start?
    };

    uint8_t register_quantum_agent_behaviors(WzBehaviorPluginApi* api);
}
