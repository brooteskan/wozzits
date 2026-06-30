#include "behavior_test_support.h"

#include <engine/behavior/quantum_agent_behaviors.h>

// End-to-end for the built-in quantum_agent decider module: config -> AgentSpec ->
// engine-side store -> self-paced think -> committed decision cached in the
// binding's instance state. Driven through the real lifecycle seams: on_init
// allocates the POD state, self.start builds + starts the agent, cognition.tick
// relaxes it on its own clock.

namespace
{
    using wz::engine::assets::SceneBehaviorConfigValue;
    using wz::engine::assets::SceneBehaviorConfigValueKind;
    using wz::engine::behavior::kQuantumAgentModule;
    using wz::engine::behavior::QuantumAgentState;

    SceneBehaviorConfigValue cfg(const char* key, double value)
    {
        return SceneBehaviorConfigValue{
            .key = key,
            .kind = SceneBehaviorConfigValueKind::Number,
            .number_value = value,
        };
    }

    // A 1-node scene whose node carries the quantum_agent module with the given
    // goal / commit policy, materialized through instantiate_scene so it has a real
    // polytree + binding id (so initialize_behaviors runs on_init).
    SceneInstance instantiate_npc(
        double goal, double confidence, double decoherence)
    {
        wz::engine::assets::SceneAssetData asset{};
        asset.name = "quantum_agent_npc";

        wz::engine::assets::SceneNodeAsset npc{};
        npc.id = "npc";
        npc.behavior = wz::engine::assets::SceneBehaviorAsset{
            .id = "npc_brain",
            .module = kQuantumAgentModule,
            .config = {
                cfg("goal", goal),
                cfg("gamma_start", 3.0),
                cfg("anneal_seconds", 4.0),
                cfg("relax_rate", 1.0),
                cfg("confidence", confidence),
                cfg("decoherence", decoherence),
                cfg("think_interval", 0.25),
            },
        };
        asset.nodes.push_back(std::move(npc));

        auto result = wz::engine::assets::instantiate_scene(asset);
        EXPECT_TRUE(result.ok()) << result.error_detail;
        return std::move(result.instance);
    }

    void run_self_start(SceneInstance& scene, BehaviorRegistry& registry)
    {
        wz::engine::FrameStorage frame_storage{};
        BehaviorFrameContext context{
            .scene = &scene,
            .behavior_state = &scene.behavior_state,
            .commands = &frame_storage.behavior_commands,
        };
        wz::engine::behavior::dispatch_self_start(scene, registry, context);
    }

    void run_tick(SceneInstance& scene, BehaviorRegistry& registry, double now)
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

    QuantumAgentState* brain(SceneInstance& scene)
    {
        auto* block = scene.behavior_state.find_instance_state("npc_brain");
        return block ? static_cast<QuantumAgentState*>(block->data) : nullptr;
    }
}

// A goal-biased NPC deliberates across its anneal and commits to the goal's
// disposition, with the committed decision cached in its instance state.
TEST(QuantumAgentBehavior, GoalDrivenNpcCommits)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_quantum_agent_behaviors));

    SceneInstance scene = instantiate_npc(
        /*goal=*/0.6, /*confidence=*/0.9, /*decoherence=*/0.0);
    initialize_behaviors(scene, registry);

    QuantumAgentState* s = brain(scene);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->started, 0u);   // on_init only allocated state
    EXPECT_EQ(s->handle, 0u);

    run_self_start(scene, registry);
    EXPECT_EQ(s->started, 1u);   // agent built + clock zeroed
    EXPECT_NE(s->handle, 0u);
    EXPECT_EQ(s->committed, -1); // has not thought yet

    // Self-paced wakes every think_interval across the anneal.
    for (int i = 1; i <= 40; ++i) {
        run_tick(scene, registry, 0.25 * i);
    }

    EXPECT_EQ(s->committed, 0);       // |0>, the goal's disposition (goal > 0)
    EXPECT_GT(s->marginal, 0.8f);     // strongly polarized +z
}

// Environmental pressure forces a snap decision even with no goal and an
// unreachable confidence threshold.
TEST(QuantumAgentBehavior, DecoherenceForcesACommit)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_quantum_agent_behaviors));

    SceneInstance scene = instantiate_npc(
        /*goal=*/0.0, /*confidence=*/0.99, /*decoherence=*/5.0);
    initialize_behaviors(scene, registry);
    run_self_start(scene, registry);

    for (int i = 1; i <= 10; ++i) {
        run_tick(scene, registry, 0.25 * i);
    }

    QuantumAgentState* s = brain(scene);
    ASSERT_NE(s, nullptr);
    EXPECT_NE(s->committed, -1);   // collapsed under pressure
}

// cognition.tick is gated on self.start: an agent that was never started does no
// thinking even though the scheduler fires it.
TEST(QuantumAgentBehavior, ThinkingIsGatedOnSelfStart)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_quantum_agent_behaviors));

    SceneInstance scene = instantiate_npc(
        /*goal=*/0.6, /*confidence=*/0.9, /*decoherence=*/0.0);
    initialize_behaviors(scene, registry);
    // No self.start dispatched.

    run_tick(scene, registry, 0.25);
    run_tick(scene, registry, 0.5);

    QuantumAgentState* s = brain(scene);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->started, 0u);
    EXPECT_EQ(s->handle, 0u);
    EXPECT_EQ(s->committed, -1);   // never deliberated
}
