#include <engine/behavior/quantum_agent_behaviors.h>

#include <engine/behavior/behavior_module_api.h>
#include <engine/cognition/agent_cognition.h>

#include <optional>

namespace wz::engine::behavior
{
    namespace
    {
        // The engine-side owner of every quantum_agent's wave function. A single
        // process-wide store keyed by the POD handle each binding holds in its
        // instance state; it outlives scene rebuilds (the handle in the preserved
        // instance state stays valid).
        wz::cognition::AgentCognitionStore& store()
        {
            static wz::cognition::AgentCognitionStore instance;
            return instance;
        }

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
                // and zero its deliberation clock at the current sim-time.
                wz::cognition::AgentSpec spec;
                spec.agent_count = 1;
                spec.goals = {
                    wz::cognition::Goal{
                        .agent = 0u,
                        .field = config_float(facts, kQuantumAgentGoalKey, 0.0f),
                    },
                };
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
                spec.chi = 0;  // exact single-agent backend

                const wz::cognition::AgentHandle handle = store().create(spec);
                if (handle == wz::cognition::kInvalidAgent) {
                    return;
                }
                store().start(handle, wz_sim_time(facts));

                const float interval =
                    config_float(facts, kQuantumAgentThinkIntervalKey, 0.25f);
                state->handle = handle;
                state->think_interval = interval > 0.0f ? interval : 0.25f;
                state->committed = -1;
                state->marginal = 0.0f;
                state->started = 1;
                return;
            }

            if (event->kind == WZ_EVENT_COGNITION_TICK) {
                if (!state->started || state->handle == 0u) {
                    return;
                }
                // One self-paced deliberation step, then cache the decision for the
                // frame-path readers and schedule the next wake.
                store().think(state->handle, wz_sim_time(facts));
                const std::optional<bool> decided =
                    store().committed(state->handle, 0u);
                state->committed = decided.has_value()
                    ? static_cast<int8_t>(*decided ? 1 : 0)
                    : static_cast<int8_t>(-1);
                state->marginal =
                    static_cast<float>(store().marginal(state->handle, 0u));
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
