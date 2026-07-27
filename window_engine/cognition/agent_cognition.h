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

#include <cognition/agent_layout.h>
#include <cognition/commit.h>
#include <cognition/coordination.h>
#include <cognition/cognition_clock.h>
#include <cognition/exact_group.h>  // ExactBond, Goal
#include <cognition/learning.h>      // LearnedTable
#include <cognition/qstate/qstate.h>          // Rng

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace wz::engine::cognition
{
    using AgentHandle = uint64_t;
    inline constexpr AgentHandle kInvalidAgent = 0;

    // The exact backend's TIME budget, in qubits. Distinct from the 2^n memory
    // guard inside build_exact (24 qubits ~ 256 MB): memory is not what breaks
    // first -- the memory cap sits about ten qubits past the point where an agent
    // can still run in a game loop. A group over THIS cap is promoted off the exact
    // backend rather than refused (see build_coordination); backend_chi() reports
    // what was actually built.
    //
    // It is BUILD-DEPENDENT because it is a time budget and time depends on the
    // optimization level -- and the difference is ~48x, far too large for one
    // number to serve both. Each tier is set where one think() (5 substeps at the
    // default 0.25 s cadence) costs roughly 3 ms. Measured 2026-07-26 on a chain:
    //
    //   optimized (/O2)          debug (clang-debug, what the engine ships)
    //      8q    0.073 ms            8q     2.9 ms   <- cap
    //     12q    1.573 ms            9q     6.6 ms
    //     13q   ~3     ms   <- cap  10q    14.8 ms
    //     16q   47.6   ms           12q    66.0 ms
    //     18q    256   ms           13q   142.6 ms
    //     20q   1703   ms           14q   334.1 ms
    //
    // Consequence worth knowing: a group between the two caps runs exact in a
    // release build and promoted in a debug one, so its fidelity differs by build.
    // That is deliberate -- the alternative is the editor stalling 143 ms per think
    // to preserve a fidelity the frame cannot pay for -- and the quantum_agent
    // module logs the promotion either way. Re-measure before changing these; they
    // are budgets, not sizeofs.
#if defined(NDEBUG)
    inline constexpr uint32_t kMaxExactQubitsRuntime = 13;
#else
    inline constexpr uint32_t kMaxExactQubitsRuntime = 8;
#endif

    // The chi a promoted nearest-neighbour chain lands on. 2 is the cheapest
    // setting that carries real entanglement (a chain of 32 at chi=2 is 0.55 ms
    // vs 2.47 ms at chi=4), and on a chain there are no frustrated loops for
    // Vidal simple update to over-polarize.
    inline constexpr uint32_t kPromotedChainChi = 2;

    // Declarative description of one coordinated group the store will deliberate.
    // chi selects the coordination backend:
    //   chi == 0  -- exact joint state (genuine entanglement, small groups);
    //   chi == 1  -- LoopyBpNetwork (chi = 1 damped loopy BP): any topology incl.
    //                cyclic villages, scales linearly, but a product-state
    //                approximation with NO entanglement;
    //   chi >= 2  -- bounded entanglement on ANY topology. A nearest-neighbour
    //                chain takes the TTN chain specialization (measured ~8x
    //                cheaper on that shape); a ring, star or arbitrary graph takes
    //                the general graph TN. Both are chi-truncated, so the dial
    //                reads continuously: 0 = full fidelity, 1 = none, >= 2 = as
    //                much as chi buys, whatever the topology.
    struct AgentSpec
    {
        // QUBIT count. With no layout below, agent i IS qubit i (one binary
        // disposition each) -- the model every existing mind uses. With a layout,
        // this must equal its total_qubits or create() refuses the spec.
        uint32_t agent_count = 1;
        std::vector<ExactBond> bonds;  // pairwise couplings (a, b, j) -- QUBIT indices
        std::vector<Goal> goals;       // per-QUBIT longitudinal goal biases
        CognitionClock clock;          // anneal sweep + relaxation pacing
        CommitPolicy commit;           // when a disposition collapses
        uint32_t chi = 0;              // backend selector (see above)
        uint64_t seed = 0x9e3779b97f4a7c15ull;  // rng for decoherence collapse
        // Optional LEARNING: a table of this many remembered binary FACTS, held
        // outside the coordination, so it accumulates across commits / rearms /
        // reshapes. reward() concentrates it; memory_preference() reads it back
        // (feed as a goal to bias decisions). 0 = no memory, capped at
        // kMaxMemoryBits. It is a classical weight table -- see learning.h.
        uint32_t memory_bits = 0;

        // MULTI-DISPOSITION AGENTS. Empty (the default) means one qubit per agent:
        // one binary yes/no choice each, and every existing mind is unchanged.
        // Non-empty means agent i owns dispositions_per_agent[i] contiguous qubits
        // -- so an agent can hold a real >2-way choice (flee | fight | hide) rather
        // than a bit. Bonds and goals stay QUBIT-indexed; qubit_index() maps
        // (agent, disposition) to the flat index, and the read surface takes
        // either form. agent_count must equal the sum.
        std::vector<uint32_t> dispositions_per_agent;

        // Per-agent soft one-hot strength (parallel to dispositions_per_agent;
        // empty or 0 = none). Non-zero makes that agent's dispositions MUTUALLY
        // EXCLUSIVE: exactly one active, chosen by whichever goals break the tie,
        // and a symmetric superposition of the k one-hot options without one. It
        // encodes the penalty A * (N_active - 1)^2 as pairwise antiferro bonds plus
        // an inactive bias (see exclusivity.h), expanded at BUILD time so the terms
        // pass through canonicalize_bonds with the authored ones and work on every
        // backend. Derived every build, never persisted -- set_goal() overwrites a
        // goal slot outright, so a stored bias would be silently wiped by a live
        // re-bias.
        std::vector<double> one_hot_strength;
    };

    // Owns agents' cognition state. Handles are opaque, non-reused while alive, and
    // 0 is never a valid handle.
    class AgentCognitionStore
    {
    public:
        // Build the agent's backend + clock from the spec. Returns a fresh handle,
        // or kInvalidAgent if the spec is unbuildable (bad agent_count, or chi >= 2
        // TTN bonds that are not a nearest-neighbour chain).
        //
        // A chi == 0 spec whose agent_count exceeds kMaxExactQubitsRuntime is
        // PROMOTED to a linear-scaling backend rather than refused; compare
        // spec.chi against backend_chi(handle) to detect it and tell the author.
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

        // Chart-timed projective measurement of decision `agent` along axis theta
        // (x-z plane), WITH back-action. Unlike think()'s self-timed commit under
        // the policy, the caller times this and CHOOSES the basis -- the non-
        // commuting readout that, on an entanglement-capable mind (chi = 0, or
        // chi >= 2 capturing the state), yields correlations no classical machine
        // reproduces. Latches the outcome as the committed bit (so committed() /
        // marginal read it) and returns it; nullopt for a bad handle / out-of-range
        // agent. The latch supersedes the commit policy for this slot until the next
        // rearm(); a subsequent think() re-projects it in the z basis, so the
        // intended protocol is measure -> read -> rearm within one "shot".
        std::optional<bool> measure_in_basis(
            AgentHandle h, uint32_t agent, double theta);

        // LEARNING. Reinforce one branch of remembered fact `memory_bit`.
        //
        // VALUE SEMANTICS, throughout this API: `value` names the branch being
        // reinforced -- value == true is the 1 branch (driving memory_preference
        // toward -1), value == false is the 0 branch (toward +1). reward_pair and
        // conditional_preference below use the same mapping.
        //
        // This USED TO BE INVERTED relative to reward_pair: the parameter was
        // called `toward` and toward == true selected the 0 branch, so the two
        // halves of one learning API meant opposite things by the same bool. The
        // header documented the trap rather than fixing it. If you are porting
        // caller code, flip every reward() bool; the behavior ABI version was
        // bumped so a stale plugin fails to load rather than silently learning
        // backwards.
        //
        // `strength` > 0 rewards, < 0 punishes; monotonic and saturating, with no
        // magnitude at which it misbehaves. Untouched by rearm/reshape/commit --
        // the learned bias accumulates. False if the agent has no memory / bad bit.
        bool reward(
            AgentHandle h, uint32_t memory_bit, bool value, double strength);

        // Read what the table learned about `memory_bit`: <sigma_z> in [-1, 1]
        // (+1 = leans to the 0 branch, -1 = to the 1 branch). Feed it, scaled, as
        // a decision goal. 0 if no memory / bad bit.
        double memory_preference(AgentHandle h, uint32_t memory_bit) const;

        // CONTEXTUAL LEARNING. Reinforce the JOINT branch (ctx_bit == ctx_value AND
        // dec_bit == dec_value). Learning the "diagonal" -- reward (ctx0, act0) and
        // (ctx1, act1) -- produces a table whose action is undecided marginally but
        // definite GIVEN the context, which an independent (product) table cannot
        // represent. False if the agent has no memory / bad bit.
        bool reward_pair(
            AgentHandle h,
            uint32_t ctx_bit, bool ctx_value,
            uint32_t dec_bit, bool dec_value,
            double strength);

        // Read the learned action for a context: <sigma_z> of dec_bit GIVEN
        // ctx_bit == ctx_value, in [-1, 1] (+1 => the 0 branch). Feed it, scaled,
        // as the decision's goal so the agent acts on the policy for the CURRENT
        // context. Reads without disturbing the table, and is O(1) between rewards.
        // 0 if no memory / bad bit / that context ~never occurs.
        double conditional_preference(
            AgentHandle h,
            uint32_t ctx_bit, bool ctx_value,
            uint32_t dec_bit) const;

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
        //
        // The two-argument forms are QUBIT-indexed (unchanged: with no layout,
        // qubit == agent). The three-argument forms address one DISPOSITION of one
        // agent and are the ones to use on a laid-out mind.

        // Agent's live decision marginal <sigma_z> in [-1, 1] (0 if unknown).
        double marginal(AgentHandle h, uint32_t agent) const;
        double marginal(
            AgentHandle h, uint32_t agent, uint32_t disposition) const;

        // The agent's latched committed decision, or nullopt while still
        // deliberating / unknown. true = |1> (z < 0), false = |0> (z > 0).
        //
        // On a one-hot agent, `true` on disposition d means d is the ACTIVE
        // choice: |1> is the active branch that the exclusivity penalty allows
        // exactly one of.
        std::optional<bool> committed(AgentHandle h, uint32_t agent) const;
        std::optional<bool> committed(
            AgentHandle h, uint32_t agent, uint32_t disposition) const;

        // Flat qubit index of (agent, disposition), or nullopt for an unknown
        // handle / out-of-range address. Lets a caller that only has the flat read
        // path -- the behavior ABI, which stays qubit-indexed -- resolve a
        // disposition once and then read it cheaply.
        std::optional<uint32_t> qubit_index(
            AgentHandle h, uint32_t agent, uint32_t disposition) const;

        // How many dispositions agent `agent` owns (1 when the mind has no
        // layout). Nullopt for an unknown handle / unknown agent.
        std::optional<uint32_t> disposition_count(
            AgentHandle h, uint32_t agent) const;

        bool alive(AgentHandle h) const;
        uint32_t agent_count(AgentHandle h) const;
        std::size_t size() const { return agents_.size(); }

        // The chi of the backend actually BUILT, which differs from the authored
        // chi when a chi == 0 group was too big for the exact backend's time
        // budget and got promoted. Nullopt for an unknown handle. Recomputed on
        // every rebuild, so a reshape that shrinks back under the budget reports
        // 0 again.
        std::optional<uint32_t> backend_chi(AgentHandle h) const;

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
            // The disposition layout + its exclusivity strengths, kept so every
            // rebuild re-derives the same one-hot terms. Empty layout = one qubit
            // per agent.
            AgentLayout layout;
            std::vector<double> one_hot_strength;
            uint32_t chi = 0;            // AUTHORED chi -- what rebuilds start from
            uint32_t effective_chi = 0;  // what build_coordination actually built
            uint64_t seed = 0;
            uint32_t agent_count = 0;
            // Learning memory: a classical log-weight table held outside the
            // coordination, so it survives every rebuild (see learning.h).
            LearnedTable learned;
        };

        const Agent* find(AgentHandle h) const;
        Agent* find(AgentHandle h);

        std::unordered_map<AgentHandle, Agent> agents_;
        AgentHandle next_ = 1;
    };
}
