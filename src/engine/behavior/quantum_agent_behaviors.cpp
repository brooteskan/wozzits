#include <engine/behavior/quantum_agent_behaviors.h>

#include <engine/behavior/behavior_module_api.h>
#include <cognition/agent_cognition.h>

#include <optional>

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

        // Allocate + construct this binding's POD state on first init (defaults run);
        // a later init / reload returns the preserved block as-is.
        void quantum_agent_on_init(
            const WzBehaviorInitFacts* facts,
            WzBehaviorEntityId,
            void*)
        {
            (void)wz_instance_state<QuantumAgentState>(facts);
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
                // and zero its deliberation clock at the current sim-time. TWO
                // coupled decisions: qubit 0 (the primary disposition) + qubit 1
                // (a second disposition), entangled by an optional bond so they
                // resolve together and can frustrate each other into wavering.
                wz::engine::cognition::AgentSpec spec;
                spec.agent_count = 2;
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
                    spec.bonds = {
                        wz::engine::cognition::ExactBond{
                            .a = 0u, .b = 1u,
                            .j = static_cast<double>(coupling),
                        },
                    };
                }
                spec.clock.gamma_start =
                    config_float(facts, kQuantumAgentGammaStartKey, 2.0f);
                spec.clock.gamma_end = 0.0;
                spec.clock.anneal_seconds =
                    config_float(facts, kQuantumAgentAnnealSecondsKey, 4.0f);
                spec.clock.relax_rate =
                    config_float(facts, kQuantumAgentRelaxRateKey, 1.0f);
                spec.commit.confidence =
                    config_float(facts, kQuantumAgentConfidenceKey, 0.8f);
                spec.commit.decoherence_rate =
                    config_float(facts, kQuantumAgentDecoherenceKey, 0.0f);
                spec.chi = 0;  // exact joint state: genuine entanglement, 2 qubits

                const wz::engine::cognition::AgentHandle handle = quantum_agent_store().create(spec);
                if (handle == wz::engine::cognition::kInvalidAgent) {
                    return;
                }
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
                return;
            }

            if (event->kind == WZ_EVENT_COGNITION_TICK) {
                if (!state->started || state->handle == 0u) {
                    return;
                }
                // One self-paced deliberation step, then cache EVERY qubit's
                // decision for the frame-path readers and schedule the next wake.
                quantum_agent_store().think(state->handle, wz_sim_time(facts));
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
            { kQuantumAgentGammaStartKey, "Exploration (gamma start)",
                WZ_BEHAVIOR_PARAM_FLOAT, 2.0, nullptr },
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
