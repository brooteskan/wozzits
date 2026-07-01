#include <cognition/agent_cognition.h>

#include <cognition/commit.h>
#include <cognition/exact_group.h>
#include <cognition/qstate/qstate.h>

#include <gtest/gtest.h>

#include <optional>
#include <vector>

using namespace wz::engine::cognition;

namespace
{
    CognitionClock anneal_clock()
    {
        CognitionClock clock;
        clock.gamma_start = 3.0;
        clock.gamma_end = 0.0;
        clock.anneal_seconds = 4.0;
        clock.relax_rate = 1.0;
        clock.max_substep = 0.05;
        return clock;
    }

    // Drive an agent through a full anneal: start at 0, wake every 0.1s out to t=8
    // (past the sweep into the committed hold).
    void run_anneal(AgentCognitionStore& store, AgentHandle h)
    {
        store.start(h, 0.0);
        for (int i = 1; i <= 80; ++i) {
            store.think(h, 0.1 * i);
        }
    }
}

// A small fireteam on the exact backend: a goal on one agent propagates across the
// ferromagnetic couplings, and after the anneal every agent has committed to the
// goal's disposition.
TEST(AgentCognition, ExactFireteamCommitsToTheGoal)
{
    AgentCognitionStore store;
    AgentSpec spec;
    spec.agent_count = 3;
    spec.bonds = { ExactBond{ 0, 1, 1.0 }, ExactBond{ 1, 2, 1.0 } };
    spec.goals = { Goal{ .agent = 0, .field = 0.6 } };  // field > 0 favors |0>
    spec.clock = anneal_clock();
    spec.commit = CommitPolicy{ .confidence = 0.9, .decoherence_rate = 0.0 };
    spec.chi = 0;  // exact

    const AgentHandle h = store.create(spec);
    ASSERT_NE(h, kInvalidAgent);
    ASSERT_EQ(store.agent_count(h), 3u);

    run_anneal(store, h);

    for (uint32_t i = 0; i < 3; ++i) {
        const std::optional<bool> decided = store.committed(h, i);
        ASSERT_TRUE(decided.has_value()) << "agent " << i << " never committed";
        EXPECT_FALSE(*decided);                  // |0>, the goal's disposition
        EXPECT_GT(store.marginal(h, i), 0.8);    // strongly polarized +z
    }
}

// Re-biasing a goal and re-arming re-opens a latched decision and lets it commit
// the OTHER way -- the mechanism behind an NPC changing its mind as the world
// changes. Without rearm the latch would hold the first decision forever.
TEST(AgentCognition, SetGoalPlusRearmFlipsACommittedDecision)
{
    AgentCognitionStore store;
    AgentSpec spec;
    spec.agent_count = 1;
    spec.goals = { Goal{ .agent = 0, .field = 0.8 } };  // favors |0>
    spec.clock = anneal_clock();
    spec.commit = CommitPolicy{ .confidence = 0.9, .decoherence_rate = 0.0 };
    spec.chi = 0;

    const AgentHandle h = store.create(spec);
    ASSERT_NE(h, kInvalidAgent);

    run_anneal(store, h);
    ASSERT_TRUE(store.committed(h, 0).has_value());
    EXPECT_FALSE(*store.committed(h, 0));   // |0>

    // A stale set_goal alone must NOT change the latched decision.
    ASSERT_TRUE(store.set_goal(h, 0, -0.8));   // now favors |1>
    store.think(h, 8.1);
    EXPECT_FALSE(*store.committed(h, 0));       // still latched to |0>

    // Re-arm: clear the latch + restart the clock, then anneal again.
    ASSERT_TRUE(store.rearm(h, 8.2));
    EXPECT_FALSE(store.committed(h, 0).has_value());  // re-opened (deliberating)
    for (int i = 1; i <= 80; ++i) {
        store.think(h, 8.2 + 0.1 * i);
    }
    ASSERT_TRUE(store.committed(h, 0).has_value());
    EXPECT_TRUE(*store.committed(h, 0));    // flipped to |1>, the new goal
    EXPECT_LT(store.marginal(h, 0), -0.8);  // polarized -z

    // Unknown handle / out-of-range agent are rejected.
    EXPECT_FALSE(store.set_goal(h, 5u, 0.0));
    EXPECT_FALSE(store.rearm(kInvalidAgent, 0.0));
}

// The same group on the chi-truncated TTN chain backend reaches the same decision
// -- chi selects the backend behind one store interface.
TEST(AgentCognition, TtnChainCommitsToTheGoal)
{
    AgentCognitionStore store;
    AgentSpec spec;
    spec.agent_count = 3;
    spec.bonds = { ExactBond{ 0, 1, 1.0 }, ExactBond{ 1, 2, 1.0 } };
    spec.goals = { Goal{ .agent = 0, .field = 0.6 } };
    spec.clock = anneal_clock();
    spec.commit = CommitPolicy{ .confidence = 0.9, .decoherence_rate = 0.0 };
    spec.chi = 4;  // TTN chain

    const AgentHandle h = store.create(spec);
    ASSERT_NE(h, kInvalidAgent);

    run_anneal(store, h);

    for (uint32_t i = 0; i < 3; ++i) {
        const std::optional<bool> decided = store.committed(h, i);
        ASSERT_TRUE(decided.has_value());
        EXPECT_FALSE(*decided);
        EXPECT_GT(store.marginal(h, i), 0.8);
    }
}

// Once a disposition is committed it stays latched even as deliberation continues,
// while the live marginal is still readable.
TEST(AgentCognition, CommitmentLatches)
{
    AgentCognitionStore store;
    AgentSpec spec;
    spec.agent_count = 1;
    spec.goals = { Goal{ .agent = 0, .field = 0.6 } };
    spec.clock = anneal_clock();
    spec.commit = CommitPolicy{ .confidence = 0.9, .decoherence_rate = 0.0 };
    spec.chi = 0;

    const AgentHandle h = store.create(spec);
    ASSERT_NE(h, kInvalidAgent);
    run_anneal(store, h);

    const std::optional<bool> decided = store.committed(h, 0);
    ASSERT_TRUE(decided.has_value());

    // Keep thinking; the commitment must not change.
    for (int i = 81; i <= 120; ++i) {
        store.think(h, 0.1 * i);
    }
    EXPECT_EQ(store.committed(h, 0), decided);
}

// Environmental pressure (decoherence) forces a snap decision even when the agent
// is nowhere near the confidence threshold (no goal, high Gamma -> z stays ~0).
TEST(AgentCognition, DecoherenceForcesAnEarlyCommit)
{
    AgentCognitionStore store;
    AgentSpec spec;
    spec.agent_count = 1;
    spec.clock = anneal_clock();
    spec.commit = CommitPolicy{ .confidence = 0.99, .decoherence_rate = 5.0 };
    spec.chi = 0;

    const AgentHandle h = store.create(spec);
    ASSERT_NE(h, kInvalidAgent);

    store.start(h, 0.0);
    // A few wakes under high pressure: prob ~ 5 * 0.1 per tick -> commits fast.
    for (int i = 1; i <= 10; ++i) {
        store.think(h, 0.1 * i);
    }
    EXPECT_TRUE(store.committed(h, 0).has_value());
}

// think() before start() lazily zeroes the clock and does no work; nothing
// commits at the uniform initial state with no pressure.
TEST(AgentCognition, ThinkBeforeStartIsANoOp)
{
    AgentCognitionStore store;
    AgentSpec spec;
    spec.agent_count = 1;
    spec.clock = anneal_clock();
    spec.chi = 0;

    const AgentHandle h = store.create(spec);
    ASSERT_NE(h, kInvalidAgent);

    EXPECT_EQ(store.think(h, 5.0), 0.0);
    EXPECT_FALSE(store.committed(h, 0).has_value());
}

// Unbuildable specs are rejected.
TEST(AgentCognition, RejectsUnbuildableSpecs)
{
    AgentCognitionStore store;

    AgentSpec empty;
    empty.agent_count = 0;
    EXPECT_EQ(store.create(empty), kInvalidAgent);

    AgentSpec mean_field;
    mean_field.agent_count = 2;
    mean_field.chi = 1;  // not wired yet
    EXPECT_EQ(store.create(mean_field), kInvalidAgent);

    AgentSpec non_chain;
    non_chain.agent_count = 3;
    non_chain.chi = 4;  // TTN requires a nearest-neighbour chain
    non_chain.bonds = { ExactBond{ 0, 2, 1.0 } };  // (0,2) is not a chain edge
    EXPECT_EQ(store.create(non_chain), kInvalidAgent);
}

// destroy() forgets the agent; reads after it return defaults.
TEST(AgentCognition, DestroyForgetsTheAgent)
{
    AgentCognitionStore store;
    AgentSpec spec;
    spec.agent_count = 2;
    spec.bonds = { ExactBond{ 0, 1, 1.0 } };
    spec.chi = 0;

    const AgentHandle h = store.create(spec);
    ASSERT_NE(h, kInvalidAgent);
    EXPECT_TRUE(store.alive(h));
    EXPECT_EQ(store.size(), 1u);

    EXPECT_TRUE(store.destroy(h));
    EXPECT_FALSE(store.alive(h));
    EXPECT_FALSE(store.destroy(h));  // already gone
    EXPECT_EQ(store.marginal(h, 0), 0.0);
    EXPECT_FALSE(store.committed(h, 0).has_value());
    EXPECT_EQ(store.agent_count(h), 0u);
}

// The marginal-commit policy: confident -> the leading bit (deterministic);
// undecided with no pressure -> keep deliberating.
TEST(AgentCognition, MarginalCommitPolicy)
{
    EXPECT_TRUE(confident_marginal(0.9, 0.8));
    EXPECT_FALSE(confident_marginal(0.0, 0.8));

    wz::engine::cognition::qstate::Rng rng{ 1234u };
    const CommitPolicy threshold{ .confidence = 0.8, .decoherence_rate = 0.0 };

    EXPECT_EQ(try_commit_marginal(0.95, threshold, 0.1, rng), std::optional<bool>(false));   // |0>
    EXPECT_EQ(try_commit_marginal(-0.95, threshold, 0.1, rng), std::optional<bool>(true));   // |1>
    EXPECT_EQ(try_commit_marginal(0.0, threshold, 0.1, rng), std::nullopt);                  // undecided
}
