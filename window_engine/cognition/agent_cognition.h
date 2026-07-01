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
// one place that knows a concrete backend (this first cut builds the exact joint-
// state backend for small groups and the chi-truncated TTN chain for larger ones).

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
    // chi selects the coordination backend: 0 = exact joint state (genuine
    // entanglement, small groups); >= 2 = chi-truncated TTN chain (bonds MUST form
    // the nearest-neighbour chain (i, i+1); larger groups). chi == 1 (mean-field)
    // is not wired yet -- its goal support is a follow-up -- and create() rejects it.
    struct AgentSpec
    {
        uint32_t agent_count = 1;
        std::vector<ExactBond> bonds;  // pairwise couplings (a, b, j)
        std::vector<Goal> goals;       // per-agent longitudinal goal biases
        CognitionClock clock;          // anneal sweep + relaxation pacing
        CommitPolicy commit;           // when a disposition collapses
        uint32_t chi = 0;              // backend selector (see above)
        uint64_t seed = 0x9e3779b97f4a7c15ull;  // rng for decoherence collapse
    };

    // Owns agents' cognition state. Handles are opaque, non-reused while alive, and
    // 0 is never a valid handle.
    class AgentCognitionStore
    {
    public:
        // Build the agent's backend + clock from the spec. Returns a fresh handle,
        // or kInvalidAgent if the spec is unbuildable (bad agent_count, unsupported
        // chi, or chi-TTN bonds that are not a chain).
        AgentHandle create(const AgentSpec& spec);

        // Forget an agent (it left the scene). Returns false if unknown.
        bool destroy(AgentHandle h);

        // self.start: zero the agent's deliberation clock at sim-time `now`.
        bool start(AgentHandle h, double now);

        // cognition.tick: relax the agent to sim-time `now`, then try to commit
        // each still-undecided disposition under the policy. Returns the imaginary-
        // time advanced (0 for an unknown handle or no elapsed sim-time).
        double think(AgentHandle h, double now);

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
            uint32_t agent_count = 0;
        };

        const Agent* find(AgentHandle h) const;
        Agent* find(AgentHandle h);

        std::unordered_map<AgentHandle, Agent> agents_;
        AgentHandle next_ = 1;
    };
}
