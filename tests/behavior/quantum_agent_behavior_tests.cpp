#include "behavior_test_support.h"

#include <engine/behavior/quantum_agent_behaviors.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

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
                // Pinned to the CLASSICAL limit. The shipping default leaves a
                // residual transverse field, under which a goal-biased agent
                // settles at <sigma_z> = h/sqrt(h^2 + gamma_end^2) rather than
                // at full polarization -- these tests assert the fully-decided
                // end state, so they say so instead of riding the default.
                // ResidualGammaEndLeavesTheAgentShortOfCertainty covers the
                // default.
                cfg("gamma_end", 0.0),
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

    // An NPC with TWO coupled decisions: qubit 0 (goal) + qubit 1 (posture_goal),
    // entangled by a bond (coupling).
    SceneInstance instantiate_coupled_npc(
        double goal, double posture_goal, double coupling)
    {
        wz::engine::assets::SceneAssetData asset{};
        asset.name = "quantum_agent_coupled_npc";

        wz::engine::assets::SceneNodeAsset npc{};
        npc.id = "npc";
        npc.behavior = wz::engine::assets::SceneBehaviorAsset{
            .id = "npc_brain",
            .module = kQuantumAgentModule,
            .config = {
                cfg("goal", goal),
                cfg("posture_goal", posture_goal),
                cfg("coupling", coupling),
                cfg("gamma_start", 3.0),
                cfg("gamma_end", 0.0),   // classical limit -- see instantiate_npc
                cfg("anneal_seconds", 4.0),
                cfg("relax_rate", 1.0),
                cfg("confidence", 0.9),
                cfg("decoherence", 0.0),
                cfg("think_interval", 0.25),
            },
        };
        asset.nodes.push_back(std::move(npc));

        auto result = wz::engine::assets::instantiate_scene(asset);
        EXPECT_TRUE(result.ok()) << result.error_detail;
        return std::move(result.instance);
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
    EXPECT_EQ(s->committed[0], -1); // has not thought yet

    // Self-paced wakes every think_interval across the anneal.
    for (int i = 1; i <= 40; ++i) {
        run_tick(scene, registry, 0.25 * i);
    }

    EXPECT_EQ(s->committed[0], 0);       // |0>, the goal's disposition (goal > 0)
    EXPECT_GT(s->marginal[0], 0.8f);     // strongly polarized +z
}

// A GROUP agent: a hub qubit (0) star-bonded to member qubits. The hub's goal
// drives its decision, and the ferromagnetic star drags every member to agree --
// one entangled wave function over the whole squad (the command node's mechanism).
TEST(QuantumAgentBehavior, StarCouplingEntanglesGroupMembersToTheHub)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_quantum_agent_behaviors));

    wz::engine::assets::SceneAssetData asset{};
    asset.name = "quantum_agent_group";
    wz::engine::assets::SceneNodeAsset npc{};
    npc.id = "npc";
    npc.behavior = wz::engine::assets::SceneBehaviorAsset{
        .id = "npc_brain",
        .module = kQuantumAgentModule,
        .config = {
            cfg("decisions", 3.0),        // hub (0) + two members (1,2)
            cfg("goal", 0.8),             // bias the hub toward |0>
            cfg("star_coupling", 1.5),    // hub bonded to every member
            cfg("gamma_start", 3.0),
            cfg("gamma_end", 0.0),        // classical limit -- see instantiate_npc
            cfg("anneal_seconds", 4.0),
            cfg("confidence", 0.9),
            cfg("decoherence", 0.0),      // commit on confidence alone
            cfg("think_interval", 0.25),
        },
    };
    asset.nodes.push_back(std::move(npc));
    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance scene = std::move(result.instance);

    initialize_behaviors(scene, registry);
    run_self_start(scene, registry);
    QuantumAgentState* s = brain(scene);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->agent_count, 3u);

    for (int i = 1; i <= 40; ++i) {
        run_tick(scene, registry, 0.25 * i);
    }

    // Hub committed to its goal, and both members were dragged to agree through
    // the star -- the group resolved as one.
    EXPECT_EQ(s->committed[0], 0);
    EXPECT_EQ(s->committed[1], 0);
    EXPECT_EQ(s->committed[2], 0);
}

// Two coupled decisions resolve together, and a ferromagnetic bond makes the
// second qubit FOLLOW the first even though it has no goal of its own -- the
// hallmark of coordination through the wave function (not two independent flips).
TEST(QuantumAgentBehavior, CoupledSecondDecisionFollowsThroughBond)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_quantum_agent_behaviors));

    // qubit 0 biased to |0> (goal > 0); qubit 1 has NO goal but a strong
    // ferromagnetic coupling, so the bond should drag it to |0> alongside qubit 0.
    SceneInstance scene = instantiate_coupled_npc(
        /*goal=*/0.8, /*posture_goal=*/0.0, /*coupling=*/1.5);
    initialize_behaviors(scene, registry);
    run_self_start(scene, registry);

    QuantumAgentState* s = brain(scene);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->agent_count, 2u);   // two coupled decisions built

    for (int i = 1; i <= 40; ++i) {
        run_tick(scene, registry, 0.25 * i);
    }

    EXPECT_EQ(s->committed[0], 0);            // goal's disposition
    EXPECT_EQ(s->committed[1], 0);            // dragged along by the bond
    EXPECT_GT(s->marginal[0], 0.8f);
    EXPECT_GT(s->marginal[1], 0.0f);         // pulled toward +z with qubit 0
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
    EXPECT_NE(s->committed[0], -1);   // collapsed under pressure
}

// The SHIPPING default leaves a residual transverse field at the end of the
// sweep, so a goal-biased agent lands short of certainty instead of collapsing
// to a definite classical configuration: <sigma_z> settles at
// h/sqrt(h^2 + gamma_end^2), not at 1. That margin is what "still deciding"
// means, and it is the only reason a coupled partner stays correlated at commit
// time -- at gamma_end = 0 the Hamiltonian is purely diagonal and there is no
// quantum structure left to read.
TEST(QuantumAgentBehavior, ResidualGammaEndLeavesTheAgentShortOfCertainty)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_quantum_agent_behaviors));

    // No gamma_end key -> the default. Confidence is unreachable and decoherence
    // is off, so nothing collapses the state and we read the anneal's end point.
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "quantum_agent_residual";
    wz::engine::assets::SceneNodeAsset npc{};
    npc.id = "npc";
    npc.behavior = wz::engine::assets::SceneBehaviorAsset{
        .id = "npc_brain",
        .module = kQuantumAgentModule,
        .config = {
            cfg("goal", 0.6),
            cfg("gamma_start", 3.0),
            cfg("anneal_seconds", 4.0),
            cfg("relax_rate", 1.0),
            cfg("confidence", 1.1),      // unreachable: never commits
            cfg("decoherence", 0.0),     // and nothing forces it
            cfg("think_interval", 0.25),
        },
    };
    asset.nodes.push_back(std::move(npc));
    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance scene = std::move(result.instance);

    initialize_behaviors(scene, registry);
    run_self_start(scene, registry);
    for (int i = 1; i <= 40; ++i) {
        run_tick(scene, registry, 0.25 * i);
    }

    QuantumAgentState* s = brain(scene);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->committed[0], -1);   // still deliberating, by construction

    // h / sqrt(h^2 + gamma_end^2) for h = 0.6, gamma_end = the default.
    const double h = 0.6;
    const double g = wz::engine::behavior::kQuantumAgentDefaultGammaEnd;
    const double expected = h / std::sqrt(h * h + g * g);
    EXPECT_NEAR(s->marginal[0], static_cast<float>(expected), 0.05f);
    EXPECT_LT(s->marginal[0], 0.9f);   // decidedly short of the classical limit
    EXPECT_GT(s->marginal[0], 0.3f);   // but it did lean toward the goal
}

// Agents created in the SAME frame must not think in lockstep. The tick handler
// reschedules with a fixed delay, so the phase an agent starts on is the phase it
// keeps for the life of the scene -- a squad spawn would otherwise put its whole
// cost on one frame every think_interval, which is the spike the self-paced
// scheduler exists to avoid.
TEST(QuantumAgentBehavior, AgentsStartedTogetherGetDistinctWakePhases)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_quantum_agent_behaviors));

    constexpr double kThinkInterval = 0.25;
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "quantum_agent_squad";
    for (int i = 0; i < 4; ++i) {
        wz::engine::assets::SceneNodeAsset npc{};
        npc.id = "npc" + std::to_string(i);
        npc.behavior = wz::engine::assets::SceneBehaviorAsset{
            .id = "brain" + std::to_string(i),
            .module = kQuantumAgentModule,
            .config = {
                cfg("goal", 0.6),
                cfg("think_interval", kThinkInterval),
            },
        };
        asset.nodes.push_back(std::move(npc));
    }
    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance scene = std::move(result.instance);

    initialize_behaviors(scene, registry);
    run_self_start(scene, registry);

    std::vector<double> wakes;
    for (int i = 0; i < 4; ++i) {
        const double wake = scene.behavior_state.next_wake_or(
            "brain" + std::to_string(i), -1.0);
        // Every agent is scheduled, and within one interval of the start -- the
        // phase offset delays the first think, it does not skip one.
        EXPECT_GE(wake, 0.0);
        EXPECT_LT(wake, kThinkInterval);
        wakes.push_back(wake);
    }
    std::sort(wakes.begin(), wakes.end());
    EXPECT_EQ(std::adjacent_find(wakes.begin(), wakes.end()), wakes.end())
        << "identically-configured agents landed on the same wake phase";
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
    EXPECT_EQ(s->committed[0], -1);   // never deliberated
}

// The canonical NPC decision straight from the inspector: `decisions` + `one_hot`
// turn an agent's qubits from that many independent yes/no dispositions into ONE
// mutually-exclusive choice -- flee | fight | hide, pick one. Disposition d is
// qubit d, and `committed(d) == true` (|1>) means d is the chosen one.
TEST(QuantumAgentBehavior, OneHotMakesTheDecisionsMutuallyExclusive)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_quantum_agent_behaviors));

    wz::engine::assets::SceneAssetData asset{};
    asset.name = "quantum_agent_one_hot";
    wz::engine::assets::SceneNodeAsset npc{};
    npc.id = "npc";
    npc.behavior = wz::engine::assets::SceneBehaviorAsset{
        .id = "npc_brain",
        .module = kQuantumAgentModule,
        .config = {
            cfg("decisions", 3.0),        // flee | fight | hide
            cfg("one_hot", 2.0),          // pick exactly one
            // Argue for disposition 1 (fight). |1> is the ACTIVE branch, so an
            // active-favouring goal is negative.
            cfg("posture_goal", -1.0),    // posture_goal biases qubit 1
            cfg("gamma_start", 3.0),
            cfg("gamma_end", 0.0),
            cfg("anneal_seconds", 4.0),
            cfg("confidence", 0.8),
            cfg("decoherence", 0.0),
            cfg("think_interval", 0.25),
        },
    };
    asset.nodes.push_back(std::move(npc));
    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance scene = std::move(result.instance);

    initialize_behaviors(scene, registry);
    run_self_start(scene, registry);
    QuantumAgentState* s = brain(scene);
    ASSERT_NE(s, nullptr);
    ASSERT_EQ(s->agent_count, 3u);   // three dispositions of ONE agent

    for (int i = 1; i <= 40; ++i) {
        run_tick(scene, registry, 0.25 * i);
    }

    // Exactly one active, and it is the one the tiebreaker argued for. The frame
    // cache stays flat-indexed, so disposition d reads as slot d.
    EXPECT_EQ(s->committed[0], 0);   // flee: not chosen
    EXPECT_EQ(s->committed[1], 1);   // fight: chosen (|1> == active)
    EXPECT_EQ(s->committed[2], 0);   // hide: not chosen
}

// Without one_hot the same three decisions stay INDEPENDENT -- the pre-existing
// model, and proof the exclusivity is what does the work rather than the goal.
TEST(QuantumAgentBehavior, WithoutOneHotTheDecisionsStayIndependent)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_quantum_agent_behaviors));

    wz::engine::assets::SceneAssetData asset{};
    asset.name = "quantum_agent_independent";
    wz::engine::assets::SceneNodeAsset npc{};
    npc.id = "npc";
    npc.behavior = wz::engine::assets::SceneBehaviorAsset{
        .id = "npc_brain",
        .module = kQuantumAgentModule,
        .config = {
            cfg("decisions", 3.0),
            cfg("posture_goal", -1.0),
            cfg("goal", -1.0),            // qubit 0 also argued active
            cfg("gamma_start", 3.0),
            cfg("gamma_end", 0.0),
            cfg("anneal_seconds", 4.0),
            cfg("confidence", 0.8),
            cfg("decoherence", 0.0),
            cfg("think_interval", 0.25),
        },
    };
    asset.nodes.push_back(std::move(npc));
    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    SceneInstance scene = std::move(result.instance);

    initialize_behaviors(scene, registry);
    run_self_start(scene, registry);
    for (int i = 1; i <= 40; ++i) {
        run_tick(scene, registry, 0.25 * i);
    }

    QuantumAgentState* s = brain(scene);
    ASSERT_NE(s, nullptr);
    // BOTH argued-for decisions go active -- no exclusivity to stop them.
    EXPECT_EQ(s->committed[0], 1);
    EXPECT_EQ(s->committed[1], 1);
}
