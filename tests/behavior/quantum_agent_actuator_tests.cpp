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
        // Second coupled decision, read via the indexed seam.
        uint8_t read_ok_1 = 0;
        int seen_committed_1 = -2;
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
        WzAgentDecision posture{};
        const uint8_t ok1 = wz_self_agent_decision_at(facts, event, 1u, &posture);
        if (g_actuator_probe) {
            g_actuator_probe->read_ok = ok;
            if (ok) {
                g_actuator_probe->seen_committed = decision.committed;
                g_actuator_probe->seen_marginal = decision.marginal;
            }
            g_actuator_probe->read_ok_1 = ok1;
            if (ok1) {
                g_actuator_probe->seen_committed_1 = posture.committed;
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

    // A one-shot behavior that RE-BIASES + RE-ARMS the co-located agent through the
    // write seam (wz_self_set_agent_goal / wz_self_rearm_agent) -- the sense->think
    // half of the loop. It skips the FIRST frame.update (so the test can read the
    // agent's ORIGINAL committed decision first), fires on the SECOND, then latches
    // `done` so it doesn't re-arm every frame.
    struct RearmProbe
    {
        uint8_t set_ok = 0;
        uint8_t rearm_ok = 0;
        int     frames = 0;
        bool    done = false;
    };
    RearmProbe* g_rearm_probe = nullptr;

    void rearm_probe_on_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event || event->kind != WZ_EVENT_FRAME_UPDATE) {
            return;
        }
        if (!g_rearm_probe || g_rearm_probe->done) {
            return;
        }
        // Let the first frame observe the original commit before re-arming.
        if (++g_rearm_probe->frames < 2) {
            return;
        }
        // Flip qubit 0's goal to favor |1>, then re-open the decision.
        g_rearm_probe->set_ok = wz_self_set_agent_goal(facts, event, 0u, -0.8f);
        g_rearm_probe->rearm_ok = wz_self_rearm_agent(facts, event);
        g_rearm_probe->done = true;
    }

    uint8_t register_rearm_probe(WzBehaviorPluginApi* api)
    {
        if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION
            || !api->register_module_desc)
        {
            return 0;
        }
        static const char* channels[] = { "frame.update" };
        const WzBehaviorModuleDesc desc{
            .size = sizeof(WzBehaviorModuleDesc),
            .module = "rearm_probe",
            .on_event = rearm_probe_on_event,
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

    // A node whose quantum_agent runs TWO coupled decisions, plus the actuator.
    SceneInstance instantiate_coupled_decider_actuator(
        double goal, double posture_goal, double coupling)
    {
        wz::engine::assets::SceneAssetData asset{};
        asset.name = "quantum_agent_coupled_actuator";

        wz::engine::assets::SceneNodeAsset npc{};
        npc.id = "npc";
        npc.behaviors.push_back(wz::engine::assets::SceneBehaviorAsset{
            .id = "npc_brain",
            .module = kQuantumAgentModule,
            .config = {
                cfg("goal", goal),
                cfg("posture_goal", posture_goal),
                cfg("coupling", coupling),
                cfg("gamma_start", 3.0),
                cfg("anneal_seconds", 4.0),
                cfg("relax_rate", 1.0),
                cfg("confidence", 0.9),
                cfg("think_interval", 0.25),
            },
        });
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

// The actuator can read a SECOND coupled decision through the indexed seam
// (wz_self_agent_decision_at). Both dispositions surface after the agent commits.
TEST(QuantumAgentActuator, ActuatorReadsCoupledSecondDecision)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_quantum_agent_behaviors));
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_decision_actuator));

    ActuatorProbe probe{};
    g_actuator_probe = &probe;

    // Both qubits biased to |0>, mildly coupled: both should commit to 0.
    SceneInstance scene = instantiate_coupled_decider_actuator(
        /*goal=*/0.8, /*posture_goal=*/0.8, /*coupling=*/0.5);
    initialize_behaviors(scene, registry);
    self_start(scene, registry);
    for (int i = 1; i <= 40; ++i) {
        cognition_tick(scene, registry, 0.25 * i);
    }

    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .behavior_state = &scene.behavior_state,
        .commands = &frame_storage.behavior_commands,
    };
    dispatch_behaviors(scene, registry, context);

    EXPECT_EQ(probe.read_ok, 1u);
    EXPECT_EQ(probe.seen_committed, 0);      // decision 0
    EXPECT_EQ(probe.read_ok_1, 1u);          // indexed read succeeded
    EXPECT_EQ(probe.seen_committed_1, 0);    // decision 1 (posture)

    g_actuator_probe = nullptr;
}

// The write seam end-to-end: a behavior re-biases + re-arms the co-located agent
// through the ABI, and after a fresh anneal the decision the actuator reads has
// FLIPPED to the new goal -- the NPC changing its mind, driven from a behavior.
TEST(QuantumAgentActuator, RearmThroughWriteSeamFlipsTheDecision)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_quantum_agent_behaviors));
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_decision_actuator));
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_rearm_probe));

    ActuatorProbe actuator{};
    RearmProbe rearm{};
    g_actuator_probe = &actuator;
    g_rearm_probe = &rearm;

    // Build a node with the agent (goal favors |0>), the rearm probe, and the
    // actuator.
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "quantum_agent_rearmable";
    wz::engine::assets::SceneNodeAsset npc{};
    npc.id = "npc";
    npc.behaviors.push_back(wz::engine::assets::SceneBehaviorAsset{
        .id = "npc_brain",
        .module = kQuantumAgentModule,
        .config = {
            cfg("goal", 0.8),
            cfg("gamma_start", 3.0),
            cfg("anneal_seconds", 4.0),
            cfg("relax_rate", 1.0),
            cfg("confidence", 0.9),
            cfg("think_interval", 0.25),
        },
    });
    npc.behaviors.push_back(wz::engine::assets::SceneBehaviorAsset{
        .id = "npc_rearm", .module = "rearm_probe" });
    npc.behaviors.push_back(wz::engine::assets::SceneBehaviorAsset{
        .id = "npc_actuator", .module = "decision_actuator" });
    asset.nodes.push_back(std::move(npc));
    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance scene = std::move(result.instance);

    initialize_behaviors(scene, registry);
    self_start(scene, registry);
    for (int i = 1; i <= 40; ++i) {
        cognition_tick(scene, registry, 0.25 * i);
    }

    auto dispatch_frame = [&]() {
        wz::engine::FrameStorage fs{};
        BehaviorFrameContext ctx{
            .frame_storage = &fs,
            .scene = &scene,
            .behavior_state = &scene.behavior_state,
            .commands = &fs.behavior_commands,
        };
        dispatch_behaviors(scene, registry, ctx);
    };

    dispatch_frame();
    EXPECT_EQ(actuator.seen_committed, 0);   // committed to the original goal |0>

    // Next frame the rearm probe re-biases (goal -> |1>) + re-arms via the ABI. The
    // re-arm REVOKES the latched decision, so the frame-path cache is reset and the
    // actuator immediately reads "deliberating" -- it must NOT keep acting on the
    // now-dead |0> order until the fresh anneal commits.
    dispatch_frame();
    EXPECT_EQ(rearm.set_ok, 1u);             // write seam resolved the agent
    EXPECT_EQ(rearm.rearm_ok, 1u);
    EXPECT_EQ(actuator.seen_committed, -1);  // rearm re-opened the decision

    // Fresh anneal from the new goal, then the actuator reads the FLIPPED decision.
    for (int i = 41; i <= 90; ++i) {
        cognition_tick(scene, registry, 0.25 * i);
    }
    dispatch_frame();
    EXPECT_EQ(actuator.seen_committed, 1);   // changed its mind: |1>

    g_actuator_probe = nullptr;
    g_rearm_probe = nullptr;
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
