#pragma once
// behavior/cognition/cognition_lamp.h
//
// The shared authoring contract for the "cognition lamp" -- the reusable actuator
// that RENDERS A WAVE FUNCTION AS GEOMETRY. Co-author it on a node that also hosts
// a `quantum_agent` (the decider); the lamp reads one of that agent's decisions and
// drives two child bodies so the decision is visible on screen:
//
//   * while DELIBERATING (committed == -1) both children are shown, each scaled to
//     ITS branch probability -- lamp_0 to P(|0>) = (1+z)/2, lamp_1 to P(|1>) =
//     (1-z)/2 (z = the live marginal <sigma_z>). The two sizes ARE the superposition.
//   * on COMMIT the winning child snaps to full size + the loser hides, and (if a
//     click clip is authored) one sound fires on the commit edge.
//
// An optional `rearm_metronome` re-opens the decision every N seconds so a demo
// cycles forever (deliberate -> commit -> re-anneal). This is the foundation the
// cognition-gallery demos and mini-characters build on; it owns no cognition math
// (that lives engine-side in the quantum_agent built-in), only the read + the visual.
//
// AUTHORING:
//   node "demo_x"
//     behaviors: quantum_agent { decisions:1, ... }, cognition_lamp { slot:0, ... }
//     children:  lamp_0 (the |0> body), lamp_1 (the |1> body)
//
// The agent it reads is normally CO-LOCATED (on the same node), but `agent_node`
// lets a lamp read a slot of an agent on ANOTHER named node -- so several lamps can
// visualize the several qubits of ONE shared group / entangled agent, each drawing
// its own pair of bodies (e.g. the Cat-State Twins: one 2-qubit agent, two twins).
//
// CONFIG KEYS (all optional; read live from the behavior's scene config):
//   slot            (number) which of the agent's decisions to read      [default 0]
//   full_scale      (number) local scale of a fully-committed / certain body [1.0]
//   rearm_metronome (number) seconds committed before re-arming; 0 = latch [0]
//   click_sound     (string) AudioSource clip name to play on commit; "" = silent
//   agent_node      (string) NAME of the node whose quantum_agent to read; ""/absent
//                            = the co-located agent on this same node   [default self]
//
// The child body NAMES are a convention (kChild0 / kChild1), resolved in SELF's
// subtree so the lamp is instance-safe (multiple demos with the same child names
// each drive their own children).

#include <cstdint>

namespace cognition_lamp
{
    inline constexpr const char* kModule = "cognition_lamp";

    // Config keys (see the file header for semantics).
    inline constexpr const char* kKeySlot = "slot";
    inline constexpr const char* kKeyFullScale = "full_scale";
    inline constexpr const char* kKeyRearmMetronome = "rearm_metronome";
    inline constexpr const char* kKeyClickSound = "click_sound";
    // NAME of the node hosting the quantum_agent to read; "" / absent = self.
    inline constexpr const char* kKeyAgentNode = "agent_node";

    // The two branch bodies, by name, found in SELF's subtree. lamp_0 = the |0>
    // outcome (committed == 0), lamp_1 = the |1> outcome (committed == 1).
    inline constexpr const char* kChild0 = "lamp_0";
    inline constexpr const char* kChild1 = "lamp_1";

    inline constexpr float kDefaultFullScale = 1.0f;

    // Per-instance actuator state. Trivially copyable: the host preserves it as raw
    // bytes across hot-reloads, so it holds plain data only (no RAII members).
    struct LampState
    {
        // Last frame's committed value, for commit-edge detection (click / log).
        // -2 = uninitialized, -1 = deliberating, 0 / 1 = the committed disposition.
        int8_t last_committed = -2;

        // Seconds spent in the committed state, for the rearm metronome.
        float time_since_commit = 0.0f;
    };
}
