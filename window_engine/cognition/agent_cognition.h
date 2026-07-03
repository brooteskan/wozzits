#pragma once

// cognition/agent_cognition.h
//
// The engine-side OWNER of NPC cognition state -- the decider's brain, the part
// that a behavior cannot hold itself. A behavior's per-instance state must be
// trivially copyable (the host preserves it as raw bytes across reloads/spawns),
// but an agent's wave function is a qstate::Register carrying a std::vector, plus
// a Coordination variant and an Rng. So the store owns that here, keyed by a POD
// AgentHandle the behavior keeps in its instance state.
//
// This is the decider half of the decider/actuator split: the quantum_agent
// behavior module drives an agent through the store on its lifecycle events --
//   self.start    -> create() once, then start() the clock;
//   cognition.tick-> think(now): relax across sim-time + try to commit;
// and an actuator behavior reads the committed decision / live marginal to issue
// motion/audio. The hot path (think/marginal) goes through the Coordination seam,
// so chi selects the backend without changing this interface; construction is the
// one place that knows a concrete backend: the exact joint-state backend for small
// groups (chi == 0), the chi = 1 loopy-BP backend for cyclic villages / large
// linear-scaling groups, and the chi-truncated TTN chain for larger entangled ones.

#include <cognition/commit.h>
#include <cognition/coordination.h>
#include <cognition/cognition_clock.h>
#include <cognition/exact_group.h>  // ExactBond, Goal
#include <cognition/qstate/qstate.h>          // Rng

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace wz::engine::cognition
{
    using AgentHandle = uint64_t;
    inline constexpr AgentHandle kInvalidAgent = 0;

    // Declarative description of one coordinated group the store will deliberate.
    // chi selects the coordination backend:
    //   chi == 0  -- exact joint state (genuine entanglement, small groups);
    //   chi == 1  -- LoopyBpNetwork (chi = 1 damped loopy BP): any topology incl.
    //                cyclic villages, scales linearly, but a product-state
    //                approximation with NO entanglement;
    //   chi >= 2  -- chi-truncated TTN chain (bonds MUST form the nearest-neighbour
    //                chain (i, i+1); larger entangled groups).
    struct AgentSpec
    {
        uint32_t agent_count = 1;
        std::vector<ExactBond> bonds;  // pairwise couplings (a, b, j)
        std::vector<Goal> goals;       // per-agent longitudinal goal biases
        CognitionClock clock;          // anneal sweep + relaxation pacing
        CommitPolicy commit;           // when a disposition collapses
        uint32_t chi = 0;              // backend selector (see above)
        uint64_t seed = 0x9e3779b97f4a7c15ull;  // rng for decoherence collapse
        // Optional LEARNING: a memory register of this many qubits, held OUTSIDE
        // the coordination -- never measured, so it accumulates across commits /
        // rearms / reshapes. reward() concentrates it; memory_preference() reads
        // it back (feed as a goal to bias decisions). 0 = no memory.
        uint32_t memory_qubits = 0;
    };

    // Owns agents' cognition state. Handles are opaque, non-reused while alive, and
    // 0 is never a valid handle.
    class AgentCognitionStore
    {
    public:
        // Build the agent's backend + clock from the spec. Returns a fresh handle,
        // or kInvalidAgent if the spec is unbuildable (bad agent_count, or chi >= 2
        // TTN bonds that are not a nearest-neighbour chain).
        AgentHandle create(const AgentSpec& spec);

        // Forget an agent (it left the scene). Returns false if unknown.
        bool destroy(AgentHandle h);

        // Drop every agent whose handle is NOT in `live` -- the sweep the host runs
        // after a scene rebuild to release agents whose bindings vanished (spawned
        // NPCs that died, nodes removed, scene swapped). Returns the count dropped.
        std::size_t retain(const std::vector<AgentHandle>& live);

        // self.start: zero the agent's deliberation clock at sim-time `now`.
        bool start(AgentHandle h, double now);

        // cognition.tick: relax the agent to sim-time `now`, then try to commit
        // each still-undecided disposition under the policy. Returns the imaginary-
        // time advanced (0 for an unknown handle or no elapsed sim-time).
        double think(AgentHandle h, double now);

        // Re-bias one decision's goal field live (e.g. from changed world state).
        // Takes effect on the next think(); pair with rearm() to actually re-open a
        // decision that has already latched. NOTE: this REPLACES that agent's goal
        // field (last-write-wins), whereas create()/AgentSpec SUM multiple goals
        // onto the same agent -- push the combined field if you mean to add.
        // Returns false for an unknown handle / out-of-range agent.
        bool set_goal(AgentHandle h, uint32_t agent, double field);

        // Re-open EVERY decision: clear the latches and restart the anneal clock at
        // sim-time `now`, so Gamma ramps up again and the agent re-deliberates from
        // its current (possibly re-biased) goals. Returns false for an unknown
        // handle.
        bool rearm(AgentHandle h, double now);

        // Set the agent's decoherence RATE live (the Poisson collapse pressure used
        // by think(): p = 1 - e^{-rate*dt} per tick). A high rate forces EARLY
        // commitment (snap decisions); ~0 lets it stay coherent until genuinely
        // confident. Lets an actuator drive "observation-forced decoherence" -- a
        // watched agent collapses fast/predictably, an unobserved one keeps
        // deliberating. Survives rearm/reshape (they keep the commit policy).
        // Returns false for an unknown handle.
        bool set_decoherence(AgentHandle h, double rate);

        // LEARNING. Reinforce the agent's memory toward (memory_qubit == `toward`)
        // by `strength` (> 0 reward, < 0 punish; monotonic + saturating). Untouched
        // by rearm/reshape/commit -- the learned bias accumulates. False if the
        // agent has no memory / bad qubit.
        bool reward(
            AgentHandle h, uint32_t memory_qubit, bool toward, double strength);

        // Read what the memory learned about `memory_qubit`: <sigma_z> in [-1, 1]
        // (+1 = leans toward |0>). Feed it, scaled, as a decision goal. 0 if no
        // memory / bad qubit.
        double memory_preference(AgentHandle h, uint32_t memory_qubit) const;

        // CONTEXTUAL LEARNING. Reinforce the JOINT branch (ctx_qubit == ctx_value
        // AND dec_qubit == dec_value) in the memory register -- learning the
        // "diagonal" (reward (ctx0,act0) + (ctx1,act1)) drives an ENTANGLED
        // conditional policy (the action depends on the context), which a product
        // memory cannot represent. False if the agent has no memory / bad qubit.
        bool reward_pair(
            AgentHandle h,
            uint32_t ctx_qubit, bool ctx_value,
            uint32_t dec_qubit, bool dec_value,
            double strength);

        // Read the learned action for a context WITHOUT measuring: <sigma_z> of
        // dec_qubit GIVEN ctx_qubit == ctx_value, in [-1, 1] (+1 => |0>). Feed it,
        // scaled, as the decision's goal so the agent acts on the policy for the
        // CURRENT context. 0 if no memory / bad qubit / that context ~never occurs.
        double conditional_preference(
            AgentHandle h,
            uint32_t ctx_qubit, bool ctx_value,
            uint32_t dec_qubit) const;

        // Change an agent's SIZE + bond structure live (dynamic group membership:
        // members join/leave a command's group). Rebuilds a fresh coordination of
        // `agent_count` qubits with `bonds`, preserving existing goal fields where
        // the count overlaps, and restarts the anneal clock. Returns false for an
        // unknown handle / zero count / unbuildable structure.
        bool reshape(
            AgentHandle h,
            uint32_t agent_count,
            const std::vector<ExactBond>& bonds,
            double now);

        // ---- read surface (marginal-oriented; uniform across backends) ----

        // Agent's live decision marginal <sigma_z> in [-1, 1] (0 if unknown).
        double marginal(AgentHandle h, uint32_t agent) const;

        // The agent's latched committed decision, or nullopt while still
        // deliberating / unknown. true = |1> (z < 0), false = |0> (z > 0).
        std::optional<bool> committed(AgentHandle h, uint32_t agent) const;

        bool alive(AgentHandle h) const;
        uint32_t agent_count(AgentHandle h) const;
        std::size_t size() const { return agents_.size(); }

    private:
        struct Agent
        {
            Coordination coordination;
            CognitionClock clock;
            CommitPolicy commit;
            wz::engine::cognition::qstate::Rng rng;
            std::vector<std::optional<bool>> latched;  // per-agent committed bit
            std::vector<double> marginal_cache;        // last think()'s <sigma_z>
            std::vector<double> goal_fields;           // live per-agent goal bias
            // Structural parts kept so rearm can rebuild a fresh coordination (the
            // goals come from goal_fields, so the spec's goals are NOT stored --
            // that avoids a stale-goals footgun after set_goal runs).
            std::vector<ExactBond> bonds;
            uint32_t chi = 0;
            uint64_t seed = 0;
            uint32_t agent_count = 0;
            // Learning memory (outside the coordination; never measured).
            wz::engine::cognition::qstate::Register memory;
            uint32_t memory_qubits = 0;
        };

        const Agent* find(AgentHandle h) const;
        Agent* find(AgentHandle h);

        std::unordered_map<AgentHandle, Agent> agents_;
        AgentHandle next_ = 1;
    };
}
