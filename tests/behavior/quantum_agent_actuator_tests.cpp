#include "behavior_test_support.h"

#include <engine/behavior/quantum_agent_behaviors.h>

// The decider/actuator split end-to-end: a quantum_agent decides, and a SEPARATE
// actuator behavior on the same node reads that decision via wz_self_agent_decision
// and acts on it (here: drives a velocity). This is the worked example for the read
// surface -- the pattern an authored actuator (or the test_mesh_001 tank) follows.

namespace
{
    using wz::engine::assets::SceneBehaviorConfigValue;
    using wz::engine::assets::SceneBehaviorConfigValueKind;
    using wz::engine::behavior::kQuantumAgentModule;

    struct ActuatorProbe
    {
        uint8_t read_ok = 0;
        int seen_committed = -2;
        float seen_marginal = -99.0f;
    };

    ActuatorProbe* g_actuator_probe = nullptr;

    // The example actuator: on each frame it reads the co-located quantum_agent's
    // decision (a cheap cached read) and turns the committed disposition into motion
    // -- |0> advances, |1> flees, undecided coasts.
    void decision_actuator_on_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event || event->kind != WZ_EVENT_FRAME_UPDATE) {
            return;
        }

        WzAgentDecision decision{};
        const uint8_t ok = wz_self_agent_decision(facts, event, &decision);
        if (g_actuator_probe) {
            g_actuator_probe->read_ok = ok;
            if (ok) {
                g_actuator_probe->seen_committed = decision.committed;
                g_actuator_probe->seen_marginal = decision.marginal;
            }
        }
        if (!ok) {
            return;  // no quantum_agent on this node
        }

        if (decision.committed == 0) {
            wz_self_set_linear_velocity(facts, event, 5.0f, 0.0f, 0.0f);
        } else if (decision.committed == 1) {
            wz_self_set_linear_velocity(facts, event, -5.0f, 0.0f, 0.0f);
        }
        // committed == -1 (deliberating): coast, issue no command.
    }

    uint8_t register_decision_actuator(WzBehaviorPluginApi* api)
    {
        if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION
            || !api->register_module_desc)
        {
            return 0;
        }
        static const char* channels[] = { "frame.update" };
        const WzBehaviorModuleDesc desc{
            .size = sizeof(WzBehaviorModuleDesc),
            .module = "decision_actuator",
            .on_event = decision_actuator_on_event,
            .event_channels = channels,
            .event_channel_count = 1u,
            .module_user_data = nullptr,
        };
        return api->register_module_desc(api->user, &desc);
    }

    SceneBehaviorConfigValue cfg(const char* key, double value)
    {
        return SceneBehaviorConfigValue{
            .key = key,
            .kind = SceneBehaviorConfigValueKind::Number,
            .number_value = value,
        };
    }

    // One node carrying BOTH a quantum_agent (the decider) and the example actuator
    // (which reads it). `with_agent` toggles the quantum_agent, to exercise the
    // no-agent path.
    SceneInstance instantiate_decider_actuator(bool with_agent)
    {
        wz::engine::assets::SceneAssetData asset{};
        asset.name = "quantum_agent_actuator";

        wz::engine::assets::SceneNodeAsset npc{};
        npc.id = "npc";
        if (with_agent) {
            npc.behaviors.push_back(wz::engine::assets::SceneBehaviorAsset{
                .id = "npc_brain",
                .module = kQuantumAgentModule,
                .config = {
                    cfg("goal", 0.6),
                    cfg("gamma_start", 3.0),
                    cfg("anneal_seconds", 4.0),
                    cfg("relax_rate", 1.0),
                    cfg("confidence", 0.9),
                    cfg("think_interval", 0.25),
                },
            });
        }
        npc.behaviors.push_back(wz::engine::assets::SceneBehaviorAsset{
            .id = "npc_actuator",
            .module = "decision_actuator",
        });
        asset.nodes.push_back(std::move(npc));

        auto result = wz::engine::assets::instantiate_scene(asset);
        EXPECT_TRUE(result.ok()) << result.error_detail;
        return std::move(result.instance);
    }

    void self_start(SceneInstance& scene, BehaviorRegistry& registry)
    {
        wz::engine::FrameStorage frame_storage{};
        BehaviorFrameContext context{
            .scene = &scene,
            .behavior_state = &scene.behavior_state,
            .commands = &frame_storage.behavior_commands,
        };
        wz::engine::behavior::dispatch_self_start(scene, registry, context);
    }

    void cognition_tick(
        SceneInstance& scene, BehaviorRegistry& registry, double now)
    {
        wz::engine::FrameStorage frame_storage{};
        BehaviorFrameContext context{
            .scene = &scene,
            .behavior_state = &scene.behavior_state,
            .commands = &frame_storage.behavior_commands,
            .sim_time = now,
        };
        wz::engine::behavior::dispatch_cognition_tick(scene, registry, context);
    }
}

// After the agent commits, the actuator reads the committed disposition and drives
// the matching motion command.
TEST(QuantumAgentActuator, ActuatorDrivesFromCommittedDecision)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_quantum_agent_behaviors));
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_decision_actuator));

    ActuatorProbe probe{};
    g_actuator_probe = &probe;

    SceneInstance scene = instantiate_decider_actuator(/*with_agent=*/true);
    initialize_behaviors(scene, registry);
    self_start(scene, registry);
    for (int i = 1; i <= 40; ++i) {
        cognition_tick(scene, registry, 0.25 * i);
    }

    // One frame: the actuator reads the decision and emits a command.
    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_context = nullptr,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .behavior_state = &scene.behavior_state,
        .commands = &frame_storage.behavior_commands,
    };
    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(probe.read_ok, 1u);
    EXPECT_EQ(probe.seen_committed, 0);          // |0>, the goal's disposition
    EXPECT_GT(probe.seen_marginal, 0.8f);

    const auto& commands = frame_storage.behavior_commands.commands;
    ASSERT_EQ(commands.size(), 1u);
    EXPECT_EQ(commands[0].kind, BehaviorCommandKind::SetLinearVelocity);
    EXPECT_FLOAT_EQ(commands[0].values[0], 5.0f);  // advance

    g_actuator_probe = nullptr;
}

// Before the agent commits, the actuator sees "deliberating" and issues no command.
TEST(QuantumAgentActuator, ActuatorCoastsWhileDeliberating)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_quantum_agent_behaviors));
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_decision_actuator));

    ActuatorProbe probe{};
    g_actuator_probe = &probe;

    SceneInstance scene = instantiate_decider_actuator(/*with_agent=*/true);
    initialize_behaviors(scene, registry);
    self_start(scene, registry);
    // No cognition ticks yet -> still superposed.

    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .behavior_state = &scene.behavior_state,
        .commands = &frame_storage.behavior_commands,
    };
    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(probe.read_ok, 1u);
    EXPECT_EQ(probe.seen_committed, -1);   // deliberating
    EXPECT_TRUE(frame_storage.behavior_commands.commands.empty());

    g_actuator_probe = nullptr;
}

// On a node with no quantum_agent the read returns false and the actuator no-ops.
TEST(QuantumAgentActuator, NoAgentReadsFalse)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_decision_actuator));

    ActuatorProbe probe{};
    g_actuator_probe = &probe;

    SceneInstance scene = instantiate_decider_actuator(/*with_agent=*/false);
    initialize_behaviors(scene, registry);

    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .behavior_state = &scene.behavior_state,
        .commands = &frame_storage.behavior_commands,
    };
    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(probe.read_ok, 0u);
    EXPECT_TRUE(frame_storage.behavior_commands.commands.empty());

    g_actuator_probe = nullptr;
}
