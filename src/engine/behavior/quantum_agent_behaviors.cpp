#include <engine/behavior/quantum_agent_behaviors.h>

#include <engine/behavior/behavior_module_api.h>
#include <engine/behavior/mind_ir.h>
#include <cognition/agent_cognition.h>

#include <optional>
#include <string>
#include <vector>

namespace wz::engine::behavior
{
    // The engine-side owner of every quantum_agent's wave function. A single
    // process-wide store keyed by the POD handle each binding holds in its instance
    // state; it outlives scene rebuilds (the handle in the preserved instance state
    // stays valid). Exposed via the header for the host read/write seams.
    wz::engine::cognition::AgentCognitionStore& quantum_agent_store()
    {
        static wz::engine::cognition::AgentCognitionStore instance;
        return instance;
    }

    namespace
    {
        float config_float(
            const WzBehaviorFrameFacts* facts,
            const char* key,
            float fallback)
        {
            float value = fallback;
            (void)wz_config_float(facts, key, &value);
            return value;
        }

        // Read a STRING config value: probe for the size, then read into a right-sized
        // buffer. Empty when the key is absent. (Mirrors statechart_runner's reader.)
        std::string read_config_string(
            const WzBehaviorFrameFacts* facts, const char* key)
        {
            char probe[256];
            uint32_t required = 0;
            wz_config_string(facts, key, probe, sizeof(probe), &required);
            if (required == 0) {
                return {};   // key absent / empty
            }
            std::vector<char> buffer(required, '\0');   // required includes the null
            uint32_t got = 0;
            wz_config_string(
                facts, key, buffer.data(),
                static_cast<uint32_t>(buffer.size()), &got);
            return std::string(buffer.data());
        }

        // Allocate + construct this binding's POD state on first init (defaults run);
        // a later init / reload returns the preserved block as-is.
        void quantum_agent_on_init(
            const WzBehaviorInitFacts* facts,
            WzBehaviorEntityId,
            void*)
        {
            (void)wz_instance_state<QuantumAgentState>(facts);
        }

        // Build the AgentSpec from this binding's SCALAR config -- the star / chain /
        // ring topology families plus the per-decision goal / anneal / commit / chi /
        // memory knobs (each key documented in quantum_agent_behaviors.h). The authored
        // mind IR path supersedes this whenever a `mind_ir` graph is present.
        void build_scalar_agent_spec(
            const WzBehaviorFrameFacts* facts,
            wz::engine::cognition::AgentSpec& spec)
        {
            float decisions_f = 2.0f;
            (void)wz_config_float(
                facts, kQuantumAgentDecisionsKey, &decisions_f);
            uint32_t decisions = decisions_f < 1.0f
                ? 1u
                : static_cast<uint32_t>(decisions_f);
            if (decisions > kQuantumAgentMaxDecisions) {
                decisions = kQuantumAgentMaxDecisions;
            }
            spec.agent_count = decisions;
            spec.goals = {
                wz::engine::cognition::Goal{
                    .agent = 0u,
                    .field = config_float(facts, kQuantumAgentGoalKey, 0.0f),
                },
                wz::engine::cognition::Goal{
                    .agent = 1u,
                    .field =
                        config_float(facts, kQuantumAgentPostureGoalKey, 0.0f),
                },
            };
            const float coupling =
                config_float(facts, kQuantumAgentCouplingKey, 0.0f);
            if (coupling != 0.0f) {
                spec.bonds.push_back(
                    wz::engine::cognition::ExactBond{
                        .a = 0u, .b = 1u,
                        .j = static_cast<double>(coupling),
                    });
            }
            // Star coupling: qubit 0 (the hub) bonded to every other qubit -> a group
            // agent whose members are entangled through the hub.
            const float star =
                config_float(facts, kQuantumAgentStarCouplingKey, 0.0f);
            if (star != 0.0f) {
                for (uint32_t i = 1; i < spec.agent_count; ++i) {
                    spec.bonds.push_back(
                        wz::engine::cognition::ExactBond{
                            .a = 0u, .b = i,
                            .j = static_cast<double>(star),
                        });
                }
            }
            // Nearest-neighbour topology: chain_coupling bonds (i, i+1) into an OPEN
            // chain (what a chi>=2 TTN needs); ring_coupling adds the closing (n-1, 0)
            // bond -> a CYCLE (run on chi=1). Use ONE instead of coupling/star.
            const float chain_j =
                config_float(facts, kQuantumAgentChainCouplingKey, 0.0f);
            const float ring_j =
                config_float(facts, kQuantumAgentRingCouplingKey, 0.0f);
            const float nn_j = chain_j != 0.0f ? chain_j : ring_j;
            if (nn_j != 0.0f) {
                for (uint32_t i = 0; i + 1 < spec.agent_count; ++i) {
                    spec.bonds.push_back(
                        wz::engine::cognition::ExactBond{
                            .a = i, .b = i + 1u,
                            .j = static_cast<double>(nn_j),
                        });
                }
                if (chain_j == 0.0f && ring_j != 0.0f
                    && spec.agent_count >= 3u) {
                    spec.bonds.push_back(
                        wz::engine::cognition::ExactBond{
                            .a = spec.agent_count - 1u, .b = 0u,
                            .j = static_cast<double>(ring_j),
                        });
                }
            }
            spec.clock.gamma_start =
                config_float(facts, kQuantumAgentGammaStartKey, 2.0f);
            spec.clock.gamma_end = config_float(
                facts, kQuantumAgentGammaEndKey,
                static_cast<float>(kQuantumAgentDefaultGammaEnd));
            spec.clock.anneal_seconds =
                config_float(facts, kQuantumAgentAnnealSecondsKey, 4.0f);
            spec.clock.relax_rate =
                config_float(facts, kQuantumAgentRelaxRateKey, 1.0f);
            spec.commit.confidence =
                config_float(facts, kQuantumAgentConfidenceKey, 0.8f);
            spec.commit.decoherence_rate =
                config_float(facts, kQuantumAgentDecoherenceKey, 0.0f);
            const float chi_f =
                config_float(facts, kQuantumAgentChiKey, 0.0f);
            spec.chi = chi_f < 0.0f ? 0u : static_cast<uint32_t>(chi_f);

            float memory_f = 0.0f;
            (void)wz_config_float(facts, kQuantumAgentMemoryKey, &memory_f);
            spec.memory_bits =
                memory_f < 1.0f ? 0u : static_cast<uint32_t>(memory_f);

            // Soft one-hot: reinterpret this agent's `decisions` qubits as ONE
            // agent holding that many MUTUALLY EXCLUSIVE dispositions, rather than
            // that many independent binary choices. This is the scalar-config route
            // to the canonical NPC decision ("flee | fight | hide -- pick one");
            // the mind IR can lay out several multi-disposition agents at once.
            const float one_hot =
                config_float(facts, kQuantumAgentOneHotKey, 0.0f);
            if (one_hot != 0.0f) {
                spec.dispositions_per_agent = { spec.agent_count };
                spec.one_hot_strength = { static_cast<double>(one_hot) };
            }
        }

        void quantum_agent_on_event(
            const WzBehaviorFrameFacts* facts,
            const WzBehaviorEvent* event,
            void*)
        {
            if (!facts || !event) {
                return;
            }
            QuantumAgentState* state = wz_instance_state<QuantumAgentState>(facts);
            if (!state) {
                return;  // no instance state (on_init did not run)
            }

            if (event->kind == WZ_EVENT_SELF_START) {
                // Build the agent's coordination state from this binding's config
                // and zero its deliberation clock at the current sim-time.
                wz::engine::cognition::AgentSpec spec;

                // A data-driven MIND IR -- an explicit graph of decision qubits, their
                // goal biases and the couplings (bonds) between them, plus backend /
                // anneal / commit / memory -- supersedes the scalar star/chain/ring
                // config. Absent -> the scalar path (existing NPCs are unchanged).
                const std::string mind_ir =
                    read_config_string(facts, kQuantumAgentMindIrKey);
                if (!mind_ir.empty()) {
                    std::string mind_err;
                    if (!parse_mind(mind_ir, spec, mind_err)) {
                        wz_log_errorf(
                            facts, "[quantum_agent] mind_ir parse FAILED: %s",
                            mind_err.c_str());
                        return;   // authored a mind but it is broken -- fail loudly
                    }
                    if (spec.agent_count > kQuantumAgentMaxDecisions) {
                        wz_log_errorf(
                            facts,
                            "[quantum_agent] mind_ir has %u qubits, over the cap of %u",
                            spec.agent_count, kQuantumAgentMaxDecisions);
                        return;
                    }
                } else {
                    build_scalar_agent_spec(facts, spec);
                }

                // Per-instance RNG seed so identically-configured NPCs do NOT
                // decohere in lockstep (the default seed gives every agent the same
                // sequence). Derive from the self entity -- distinct per concurrent
                // instance -- via a splitmix64 mix, kept non-zero for the xorshift*.
                uint64_t seed = 0x9e3779b97f4a7c15ull
                    + (static_cast<uint64_t>(wz_self(event)) + 1ull)
                        * 0x9e3779b97f4a7c15ull;
                seed ^= seed >> 30; seed *= 0xbf58476d1ce4e5b9ull;
                seed ^= seed >> 27; seed *= 0x94d049bb133111ebull;
                seed ^= seed >> 31;
                spec.seed = seed ? seed : 0x9e3779b97f4a7c15ull;

                const auto backend_name = [](uint32_t chi) {
                    return chi == 0u ? "exact" : (chi == 1u ? "loopy" : "ttn");
                };
                const char* const backend = backend_name(spec.chi);
                const wz::engine::cognition::AgentHandle handle =
                    quantum_agent_store().create(spec);
                if (handle == wz::engine::cognition::kInvalidAgent) {
                    // Loud, not silent: a zero or over-cap agent_count, an
                    // over-sized learning register, a disposition layout that
                    // disagrees with the qubit count -- the demo shows dark bodies
                    // and this line names why. Topology is no longer a reason: a
                    // chi>=2 ring or star now builds on the general graph backend.
                    wz_log_errorf(
                        facts,
                        "[quantum_agent] build FAILED chi=%u backend=%s agents=%u "
                        "bonds=%u (unbuildable spec -- check agent_count, memory "
                        "size and any disposition layout)",
                        spec.chi, backend, spec.agent_count,
                        static_cast<unsigned>(spec.bonds.size()));
                    return;
                }
                // The store PROMOTES a chi=0 group that has outgrown the exact
                // backend's frame budget onto a linear-scaling one. Say so loudly:
                // the agent still works, but its fidelity is not what was authored,
                // and a silent swap is the kind of thing that gets rediscovered
                // months later as "why doesn't the squad entangle any more".
                const std::optional<uint32_t> built =
                    quantum_agent_store().backend_chi(handle);
                if (built.has_value() && *built != spec.chi) {
                    wz_log_infof(
                        facts,
                        "[quantum_agent] chi=%u with %u agents exceeds the exact "
                        "backend's budget of %u qubits; PROMOTED to chi=%u (%s). "
                        "Fidelity is reduced -- author chi explicitly to choose.",
                        spec.chi, spec.agent_count,
                        wz::engine::cognition::kMaxExactQubitsRuntime, *built,
                        *built == 1u ? "loopy BP, no entanglement"
                                     : "TTN chain, bounded entanglement");
                }
                wz_log_infof(
                    facts,
                    "[quantum_agent] built chi=%u backend=%s agents=%u bonds=%u",
                    built.value_or(spec.chi), backend_name(built.value_or(spec.chi)),
                    spec.agent_count,
                    static_cast<unsigned>(spec.bonds.size()));
                quantum_agent_store().start(handle, wz_sim_time(facts));

                const float interval =
                    config_float(facts, kQuantumAgentThinkIntervalKey, 0.25f);
                state->handle = handle;
                state->think_interval = interval > 0.0f ? interval : 0.25f;
                state->agent_count =
                    static_cast<uint8_t>(spec.agent_count);
                for (uint32_t i = 0; i < kQuantumAgentMaxDecisions; ++i) {
                    state->committed[i] = -1;
                    state->marginal[i] = 0.0f;
                }
                state->started = 1;
                // Bootstrap the self-paced think loop from agent creation. Only the
                // cognition-tick handler reschedules the wake, and dispatch parks a
                // binding's wake at +inf before firing -- so a tick that fires BEFORE
                // this self.start early-returns (below) without rescheduling and strands
                // the wake asleep forever, and nothing else would ever wake it. Setting
                // the wake here starts the loop regardless of dispatch order.
                //
                // PHASE-OFFSET the first wake within [0, think_interval). The tick
                // handler reschedules with a FIXED delay, so whatever phase an agent
                // starts on it keeps forever: agents created in the same frame (a pool
                // prewarm, a squad spawn, a scene load) would otherwise all think on the
                // same frame, every think_interval, for the life of the scene. That is
                // the same cost spike the self-paced scheduler exists to avoid, just
                // rarer and lumpier -- 50 agents at 12 qubits is 78 ms in one frame from
                // work that averages 6.3 ms/s each. Derived from the same per-instance
                // splitmix64 mix as spec.seed, so it is deterministic per entity and
                // stable across rebuilds; the top 53 bits scaled into [0, 1).
                const float phase = static_cast<float>(
                    static_cast<double>(seed >> 11) * (1.0 / 9007199254740992.0));
                wz_set_next_wake(facts, phase * state->think_interval);
                return;
            }

            if (event->kind == WZ_EVENT_COGNITION_TICK) {
                if (!state->started || state->handle == 0u) {
                    return;
                }
                // One self-paced deliberation step, then cache EVERY qubit's
                // decision for the frame-path readers and schedule the next wake.
                quantum_agent_store().think(state->handle, wz_sim_time(facts));

                // A wrecked coordination carries no readable state, so the agent
                // has stopped committing and its marginals read NaN. Say so, ONCE
                // per occurrence: without this the only symptom is an NPC that
                // quietly never decides again, which is exactly the failure mode
                // that made this class of bug survive so long. Loud here rather
                // than in the store because this is the layer that owns a logger
                // and can name the binding.
                if (quantum_agent_store().wrecked(state->handle)) {
                    if (!state->reported_wrecked) {
                        state->reported_wrecked = 1u;
                        wz_log_errorf(
                            facts,
                            "[quantum_agent] agent %llu has an UNREADABLE state "
                            "(non-finite marginal) and has stopped deciding. A "
                            "goal, bond or clock value drove the relaxation out of "
                            "range. It does not recover on its own -- rearm or "
                            "rebuild it.",
                            static_cast<unsigned long long>(state->handle));
                    }
                } else {
                    state->reported_wrecked = 0u;   // a rebuild re-arms the report
                }

                const uint32_t count =
                    state->agent_count < kQuantumAgentMaxDecisions
                        ? state->agent_count
                        : kQuantumAgentMaxDecisions;
                for (uint32_t i = 0; i < count; ++i) {
                    const std::optional<bool> decided =
                        quantum_agent_store().committed(state->handle, i);
                    state->committed[i] = decided.has_value()
                        ? static_cast<int8_t>(*decided ? 1 : 0)
                        : static_cast<int8_t>(-1);
                    state->marginal[i] =
                        static_cast<float>(quantum_agent_store().marginal(state->handle, i));
                }
                wz_set_next_wake(facts, state->think_interval);
            }
        }
    }

    uint8_t register_quantum_agent_behaviors(WzBehaviorPluginApi* api)
    {
        if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION
            || !api->register_module_desc)
        {
            return 0;
        }

        static const char* channels[] = { "self.start", "cognition.tick" };

        // Declared tunables, so the editor can show knobs with defaults instead of
        // the author guessing config keys.
        static const WzBehaviorParamDesc params[] = {
            { kQuantumAgentGoalKey, "Goal bias",
                WZ_BEHAVIOR_PARAM_FLOAT, 0.0, nullptr },
            { kQuantumAgentPostureGoalKey, "Posture goal bias",
                WZ_BEHAVIOR_PARAM_FLOAT, 0.0, nullptr },
            { kQuantumAgentCouplingKey, "Decision coupling (bond j)",
                WZ_BEHAVIOR_PARAM_FLOAT, 0.0, nullptr },
            { kQuantumAgentDecisionsKey, "Decision count (qubits)",
                WZ_BEHAVIOR_PARAM_FLOAT, 2.0, nullptr },
            { kQuantumAgentStarCouplingKey, "Star coupling (hub bond j)",
                WZ_BEHAVIOR_PARAM_FLOAT, 0.0, nullptr },
            { kQuantumAgentChiKey, "Backend chi (0 exact / 1 loopy / >=2 TTN)",
                WZ_BEHAVIOR_PARAM_FLOAT, 0.0, nullptr },
            { kQuantumAgentChainCouplingKey, "Chain coupling (n.n. bond j)",
                WZ_BEHAVIOR_PARAM_FLOAT, 0.0, nullptr },
            { kQuantumAgentRingCouplingKey, "Ring coupling (cyclic bond j)",
                WZ_BEHAVIOR_PARAM_FLOAT, 0.0, nullptr },
            { kQuantumAgentGammaStartKey, "Exploration (gamma start)",
                WZ_BEHAVIOR_PARAM_FLOAT, 2.0, nullptr },
            { kQuantumAgentGammaEndKey, "Residual doubt (gamma end)",
                WZ_BEHAVIOR_PARAM_FLOAT, kQuantumAgentDefaultGammaEnd, nullptr },
            { kQuantumAgentAnnealSecondsKey, "Deliberation seconds",
                WZ_BEHAVIOR_PARAM_FLOAT, 4.0, nullptr },
            { kQuantumAgentRelaxRateKey, "Relax rate",
                WZ_BEHAVIOR_PARAM_FLOAT, 1.0, nullptr },
            { kQuantumAgentConfidenceKey, "Commit confidence",
                WZ_BEHAVIOR_PARAM_FLOAT, 0.8, nullptr },
            { kQuantumAgentDecoherenceKey, "Decoherence rate",
                WZ_BEHAVIOR_PARAM_FLOAT, 0.0, nullptr },
            { kQuantumAgentThinkIntervalKey, "Think interval (s)",
                WZ_BEHAVIOR_PARAM_FLOAT, 0.25, nullptr },
            { kQuantumAgentMemoryKey, "Memory qubits (learning)",
                WZ_BEHAVIOR_PARAM_FLOAT, 0.0, nullptr },
            { kQuantumAgentOneHotKey, "Exclusive choice (one-hot strength)",
                WZ_BEHAVIOR_PARAM_FLOAT, 0.0, nullptr },
        };

        WzBehaviorModuleDesc desc{};
        desc.size = sizeof(desc);
        desc.module = kQuantumAgentModule;
        desc.on_event = quantum_agent_on_event;
        desc.on_init = quantum_agent_on_init;
        desc.event_channels = channels;
        desc.event_channel_count = 2u;
        desc.module_user_data = nullptr;
        desc.params = params;
        desc.param_count = static_cast<uint32_t>(
            sizeof(params) / sizeof(params[0]));
        return api->register_module_desc(api->user, &desc);
    }
}
