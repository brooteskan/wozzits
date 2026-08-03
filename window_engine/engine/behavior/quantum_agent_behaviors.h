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
    //   gamma_end       - RESIDUAL transverse field at the end of the anneal: how
    //                     much the agent never quite makes up its mind. See the
    //                     note below -- default 0.5.
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
    inline constexpr const char* kQuantumAgentGammaEndKey = "gamma_end";
    inline constexpr const char* kQuantumAgentAnnealSecondsKey = "anneal_seconds";
    inline constexpr const char* kQuantumAgentRelaxRateKey = "relax_rate";
    inline constexpr const char* kQuantumAgentConfidenceKey = "confidence";
    inline constexpr const char* kQuantumAgentDecoherenceKey = "decoherence";
    inline constexpr const char* kQuantumAgentThinkIntervalKey = "think_interval";

    // The authoring default for the one knob that decides whether an agent has any
    // quantum structure LEFT at commit time. Shared by both front ends -- the
    // scalar config above and the mind IR (mind_ir.cpp) -- so the two cannot
    // drift. Deliberately NOT the CognitionClock struct default, which stays at 0
    // so the cognition library's own tests keep their exact, undriven baseline.
    //
    // Gamma_END is the residual transverse field the sweep lands on:
    //   * 0   -> the Hamiltonian is purely DIAGONAL at commit. Every off-diagonal
    //            term is gamma, so at gamma = 0 there is no superposition, no
    //            entanglement, and no wavering left: the agent converges to a
    //            definite classical configuration and coupled partners decorrelate.
    //            Measured on a 4-agent ferromagnetic chain with any goal set, the
    //            bipartite entanglement entropy falls to 0.158 by the end of the
    //            sweep, and a literal product state (chi=1) reproduces the exact
    //            backend's marginals to 2.1e-4 -- i.e. the entangled backends cost
    //            exponential time to compute something a product state gets right.
    //   * > 0 -> the agent keeps a soft margin: a marginal near zero means "still
    //            undecided" rather than "already decided", coupled partners stay
    //            correlated, and a frustrated group visibly wavers. 0.25-0.5 is the
    //            regime where the coupled backends are both meaningful and
    //            well-conditioned.
    // An authored mind that explicitly sets gamma_end: 0.0 keeps the old behavior.
    //
    // TUNING NOTE -- a live field interacts with `confidence`. The residual doubt
    // caps how sure an agent can ever get: a lone qubit under goal h settles at
    // <sigma_z> = h / sqrt(h^2 + gamma_end^2), i.e. a leading-outcome probability
    // of (1 + that) / 2. At the 0.5 default a goal of 0.6 tops out at P = 0.88 and
    // will NEVER clear the default confidence of 0.9. That is the intended
    // meaning -- a moderate goal against real doubt is not a confident decision --
    // but it means an agent that must decide needs a goal strong relative to
    // gamma_end, a lower confidence, or an explicit gamma_end of 0.
    //
    // The degenerate case: a PERFECTLY symmetric coupled group with no goals sits
    // at marginal 0 forever, so confidence never fires and it deliberates without
    // end (verified: an unbiased ferromagnetic pair holds z0 = z1 = 0.0000 out to
    // t = 6 s). Any goal breaks the symmetry. `decoherence` is the escape hatch if
    // an authored mind really does need to commit out of a symmetric state, but it
    // stays OFF by default: try_commit checks confidence first and then rolls
    // against the decoherence rate, so a non-zero default would mostly fire
    // MID-ANNEAL while Gamma is still high and the state is unpolarized -- turning
    // considered decisions into coin flips, which is the opposite of the point.
    inline constexpr double kQuantumAgentDefaultGammaEnd = 0.5;

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
    // product-state approximation with NO entanglement); >= 2 = bounded
    // entanglement on ANY topology -- a nearest-neighbour chain takes the cheap TTN
    // chain specialization, a ring/star/arbitrary graph takes the general graph TN.
    // Default 0. Topology is no longer a build constraint: a chi>=2 ring or star
    // builds, which is also what makes a chi>=2 group reshapable (reshape builds a
    // star).
    inline constexpr const char* kQuantumAgentChiKey = "chi";

    // Nearest-neighbour topology for the linear-scaling backends -- use ONE of these
    // INSTEAD of coupling/star for a many-membered group:
    //   chain_coupling - bond (i, i+1) into an OPEN chain (the shape that takes
    //                    the cheap chi>=2 TTN specialization). Default 0.
    //   ring_coupling  - the chain PLUS the closing bond (n-1, 0): a CYCLE. An odd
    //                    anti-ferromagnetic ring is FRUSTRATED (no 2-coloring) -- the
    //                    thing a tree cannot show. Runs on chi=1 (a product state)
    //                    or chi>=2 (the general graph TN). The closing bond is
    //                    skipped below 3 qubits (a 2-ring would double the lone bond).
    //                    Default 0.
    inline constexpr const char* kQuantumAgentChainCouplingKey = "chain_coupling";
    inline constexpr const char* kQuantumAgentRingCouplingKey = "ring_coupling";

    // EXCLUSIVITY: make this agent's `decisions` qubits a single MUTUALLY-EXCLUSIVE
    // choice -- flee | fight | hide, pick one -- instead of that many independent
    // yes/no dispositions. Non-zero is the penalty weight A on having anything
    // other than exactly one active; 0 (the default) leaves the decisions
    // independent, so every existing agent is unchanged.
    //
    // With it set, all `decisions` qubits become ONE agent's dispositions:
    // disposition d is qubit d, `committed(d) == true` means d is the CHOSEN one,
    // and a goal on qubit d is the tiebreaker that argues for it (negative favors
    // active -- |1> is the active branch). Without a tiebreaker the agent holds a
    // symmetric superposition of the k one-hot options, each ~1/k.
    //
    // Use INSTEAD of the coupling/star/chain/ring families, which wire the qubits
    // as separate coupled agents rather than one agent's alternatives. A is a
    // strength like any bond: ~2x the largest goal you expect is a reasonable
    // start, and too small lets two dispositions be active at once.
    inline constexpr const char* kQuantumAgentOneHotKey = "one_hot";

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

    // Cap on coupled decisions a single agent exposes (bounds the fixed-size, trivially-
    // copyable POD cache below). 32 supports large minds on the LINEAR-scaling backends:
    // chi=1 loopy BP is O(N), and a chi>=2 tensor network is
    // ~O(N * poly(chi)). NOT the exact backend (chi=0), whose joint state is O(2^N) -- keep
    // exact minds to a handful of qubits. The mind_ir loader rejects a graph over this cap.
    inline constexpr uint32_t kQuantumAgentMaxDecisions = 32;

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
        // Per-qubit cache ([i] = decision i; committed -1 = deliberating, else 0/1;
        // marginal = live <sigma_z> in [-1, 1]). SELF_START fills every slot on build;
        // before then only [0] is read (agent_count starts at 1), so [0] alone needs the
        // deliberating sentinel here (the rest zero-fill and are never read pre-build).
        int8_t committed[kQuantumAgentMaxDecisions] = { -1 };
        // Have we already logged that this agent's state became unreadable? The
        // store latches `wrecked` until a rebuild, so without this the tick handler
        // would log it every think_interval forever.
        //
        // Placed HERE, between the int8 array and the float array, deliberately:
        // `marginal` needs 4-byte alignment, so two padding bytes already sat at
        // this offset and the flag costs no size. Instance state is preserved as
        // raw bytes across reloads, so a layout change would misread a live blob --
        // the static_assert below pins that this stayed free.
        uint8_t reported_wrecked = 0;
        float marginal[kQuantumAgentMaxDecisions] = {};
    };

    // 8 handle + 4 interval + 1 started + 1 agent_count + 32 committed
    // + 1 reported_wrecked + 1 pad + 128 marginal.
    static_assert(sizeof(QuantumAgentState) == 176,
        "QuantumAgentState is preserved as raw bytes across reloads; a size change "
        "silently misreads live instance state. Fit new fields in the padding.");

    uint8_t register_quantum_agent_behaviors(WzBehaviorPluginApi* api);
}
