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

// Committing a decision PROJECTS the joint register, so a coupled partner is
// conditioned on the committed outcome. For a ferromagnetic cat (|00>+|11>)/sqrt2
// -- both marginals ~0, the hard case -- decoherence-forced commits must come out
// JOINTLY CONSISTENT (both 0 or both 1). Without projection the two would be
// sampled independently and land on the probability-zero joint 01/10 half the time.
TEST(AgentCognition, CoupledCommitsStayJointlyConsistent)
{
    const int trials = 100;
    int consistent = 0;
    for (int t = 0; t < trials; ++t) {
        AgentCognitionStore store;
        AgentSpec spec;
        spec.agent_count = 2;
        spec.bonds = { ExactBond{ 0, 1, 1.5 } };  // ferro -> cat, no goals
        spec.clock = anneal_clock();               // Gamma -> 0: cat purifies
        // Only decoherence commits (confidence unreachable), so both draw from the
        // undecided marginal -- the case that exposes the joint inconsistency.
        spec.commit = CommitPolicy{ .confidence = 1.1, .decoherence_rate = 1.0 };
        spec.chi = 0;
        spec.seed = 0x1234u + static_cast<uint64_t>(t);  // vary per trial

        const AgentHandle h = store.create(spec);
        ASSERT_NE(h, kInvalidAgent);
        store.start(h, 0.0);

        std::optional<bool> d0, d1;
        for (int i = 1; i <= 300 && !(d0 && d1); ++i) {
            store.think(h, 0.1 * i);
            d0 = store.committed(h, 0);
            d1 = store.committed(h, 1);
        }
        ASSERT_TRUE(d0.has_value());
        ASSERT_TRUE(d1.has_value());
        if (*d0 == *d1) {
            ++consistent;
        }
    }
    // With projection the coupled outcomes agree the large majority of the time;
    // WITHOUT it they would be independent samples off ~0 marginals => ~0.5. (Not
    // 1.0 here: decoherence can fire mid-anneal while Gamma is still high and the
    // state genuinely carries 01/10 amplitude -- the pure-cat "probability zero"
    // guarantee is tested surgically in ExactGroup.CollapseConditionsEntangledPartner.)
    EXPECT_GT(static_cast<double>(consistent) / trials, 0.7);
}

// retain() sweeps agents whose handle isn't in the live set -- the store hygiene
// the host runs after a rebuild so despawned NPCs' wave functions are released.
TEST(AgentCognition, RetainDropsAgentsNotInLiveSet)
{
    AgentCognitionStore store;
    AgentSpec spec;
    spec.agent_count = 1;
    spec.goals = { Goal{ .agent = 0, .field = 0.5 } };
    spec.clock = anneal_clock();
    spec.commit = CommitPolicy{ .confidence = 0.9 };
    spec.chi = 0;

    const AgentHandle a = store.create(spec);
    const AgentHandle b = store.create(spec);
    const AgentHandle c = store.create(spec);
    ASSERT_EQ(store.size(), 3u);

    // Keep a and c; b's binding vanished.
    const std::size_t dropped = store.retain({ a, c });
    EXPECT_EQ(dropped, 1u);
    EXPECT_EQ(store.size(), 2u);
    EXPECT_TRUE(store.alive(a));
    EXPECT_FALSE(store.alive(b));
    EXPECT_TRUE(store.alive(c));

    // Retaining nothing clears the store; an empty live set is valid.
    EXPECT_EQ(store.retain({}), 2u);
    EXPECT_EQ(store.size(), 0u);
}

// reshape() grows a hub agent to include new members, star-bonded, and the hub's
// goal drags them in -- dynamic squad membership as one entangled coordination.
TEST(AgentCognition, ReshapeGrowsAStarGroupThatEntanglesNewMembers)
{
    AgentCognitionStore store;
    AgentSpec spec;
    spec.agent_count = 1;                                 // start: just the hub
    spec.goals = { Goal{ .agent = 0, .field = 0.7 } };    // hub biased to |0>
    spec.clock = anneal_clock();
    spec.commit = CommitPolicy{ .confidence = 0.9 };
    spec.chi = 0;

    const AgentHandle h = store.create(spec);
    ASSERT_NE(h, kInvalidAgent);
    ASSERT_EQ(store.agent_count(h), 1u);

    // Two members join: hub + 2, star-bonded ferro.
    const std::vector<ExactBond> star = {
        ExactBond{ .a = 0, .b = 1, .j = 1.5 },
        ExactBond{ .a = 0, .b = 2, .j = 1.5 },
    };
    ASSERT_TRUE(store.reshape(h, /*agent_count=*/3, star, /*now=*/0.0));
    EXPECT_EQ(store.agent_count(h), 3u);

    for (int i = 1; i <= 80; ++i) {
        store.think(h, 0.1 * i);
    }

    // Hub keeps its |0> disposition (goal preserved across reshape), and both new
    // members were dragged to agree through the star.
    ASSERT_TRUE(store.committed(h, 0).has_value());
    EXPECT_FALSE(*store.committed(h, 0));   // |0>
    ASSERT_TRUE(store.committed(h, 1).has_value());
    EXPECT_FALSE(*store.committed(h, 1));
    ASSERT_TRUE(store.committed(h, 2).has_value());
    EXPECT_FALSE(*store.committed(h, 2));
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

// set_decoherence drives observation-forced collapse: the SAME agent + goal
// commits early when its decoherence is set high, and stays deliberating when set
// ~0 (only confidence could commit it, which the weak goal never reaches).
TEST(AgentCognition, SetDecoherenceControlsCommitTiming)
{
    auto run = [](double rate) {
        AgentCognitionStore store;
        AgentSpec spec;
        spec.agent_count = 1;
        spec.clock = anneal_clock();
        // High confidence so ONLY decoherence can force an early commit; a weak
        // goal so the marginal never reaches the confidence bar on its own.
        spec.commit = CommitPolicy{ .confidence = 0.99, .decoherence_rate = 0.0 };
        spec.goals = { Goal{ .agent = 0u, .field = 0.05 } };
        const AgentHandle h = store.create(spec);
        EXPECT_NE(h, kInvalidAgent);
        EXPECT_TRUE(store.set_decoherence(h, rate));
        store.start(h, 0.0);
        for (int i = 1; i <= 10; ++i) {
            store.think(h, 0.1 * i);
        }
        return store.committed(h, 0).has_value();
    };
    EXPECT_TRUE(run(8.0));    // watched: high decoherence -> snap commit
    EXPECT_FALSE(run(0.0));   // unobserved: stays coherent (weak goal, no pressure)
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

    AgentSpec non_chain;
    non_chain.agent_count = 3;
    non_chain.chi = 4;  // TTN requires a nearest-neighbour chain
    non_chain.bonds = { ExactBond{ 0, 2, 1.0 } };  // (0,2) is not a chain edge
    EXPECT_EQ(store.create(non_chain), kInvalidAgent);
}

// chi == 1 now BUILDS on the loopy-BP backend (formerly rejected). A ferro pair
// with a goal on agent 0 propagates through the coupling and both agents commit to
// the goal's disposition -- the chi == 1 path is wired through the store dispatch.
TEST(AgentCognition, ChiOneBuildsOnLoopyBackend)
{
    AgentCognitionStore store;
    AgentSpec spec;
    spec.agent_count = 2;
    spec.bonds = { ExactBond{ 0, 1, 1.0 } };
    spec.goals = { Goal{ .agent = 0, .field = 0.6 } };  // field > 0 favors |0>
    spec.clock = anneal_clock();
    spec.commit = CommitPolicy{ .confidence = 0.9, .decoherence_rate = 0.0 };
    spec.chi = 1;  // loopy-BP backend

    const AgentHandle h = store.create(spec);
    ASSERT_NE(h, kInvalidAgent);  // no longer rejected
    ASSERT_EQ(store.agent_count(h), 2u);

    run_anneal(store, h);

    // Both agents commit to |0> (the goal's disposition), and the live marginal is
    // strongly +z -- a sane decision, not a stuck/garbage state.
    for (uint32_t i = 0; i < 2; ++i) {
        const std::optional<bool> decided = store.committed(h, i);
        ASSERT_TRUE(decided.has_value()) << "chi==1 agent " << i << " never committed";
        EXPECT_FALSE(*decided) << "chi==1 agent " << i << " did not follow the +z goal";
        EXPECT_GT(store.marginal(h, i), 0.5);
    }
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

// A memory register learns from rewards: it starts unbiased (<sigma_z> == 0),
// concentrates toward the rewarded branch, and -- crucially -- keeps that learned
// bias through a rearm (which rebuilds the DECISION coordination but must leave the
// unmeasured memory untouched).
TEST(AgentCognition, MemoryLearnsAndSurvivesRearm)
{
    AgentCognitionStore store;
    AgentSpec spec;
    spec.agent_count = 1;
    spec.clock = anneal_clock();
    spec.commit = CommitPolicy{ .confidence = 0.8, .decoherence_rate = 0.0 };
    spec.memory_qubits = 1;
    const AgentHandle h = store.create(spec);
    ASSERT_NE(h, kInvalidAgent);

    // Fresh memory is unbiased.
    EXPECT_NEAR(store.memory_preference(h, 0), 0.0, 1e-9);

    // Reward toward |0> repeatedly -> preference climbs toward +1 (monotonic).
    double prev = store.memory_preference(h, 0);
    for (int i = 0; i < 8; ++i) {
        EXPECT_TRUE(store.reward(h, 0, /*toward=*/true, 0.5));
        const double now = store.memory_preference(h, 0);
        EXPECT_GT(now, prev - 1e-9);
        prev = now;
    }
    EXPECT_GT(store.memory_preference(h, 0), 0.5);

    // Rearm rebuilds the decision coordination; the learned memory must persist.
    const double learned = store.memory_preference(h, 0);
    ASSERT_TRUE(store.rearm(h, 10.0));
    EXPECT_NEAR(store.memory_preference(h, 0), learned, 1e-9);

    // Punishing (reward toward |1>) pulls it back -- relearning.
    for (int i = 0; i < 12; ++i) {
        store.reward(h, 0, /*toward=*/false, 0.5);
    }
    EXPECT_LT(store.memory_preference(h, 0), learned);
}

// Contextual learning through the store: a 2-qubit memory learns the diagonal
// policy (ctx 0 -> act 0, ctx 1 -> act 1). The conditional read then returns the
// learned action PER context (opposite signs), it survives a rearm, and an
// out-of-range qubit is rejected.
TEST(AgentCognition, ContextualPolicyLearnsAndConditions)
{
    AgentCognitionStore store;
    AgentSpec spec;
    spec.agent_count = 1;
    spec.clock = anneal_clock();
    spec.memory_qubits = 2;   // qubit 0 = context, qubit 1 = action
    const AgentHandle h = store.create(spec);
    ASSERT_NE(h, kInvalidAgent);

    // Fresh: no context-dependence either way.
    EXPECT_NEAR(store.conditional_preference(h, 0, false, 1), 0.0, 1e-6);

    // Reward the diagonal.
    for (int i = 0; i < 40; ++i) {
        EXPECT_TRUE(store.reward_pair(h, 0, false, 1, false, 0.5));  // ctx0 -> act0
        EXPECT_TRUE(store.reward_pair(h, 0, true, 1, true, 0.5));    // ctx1 -> act1
    }
    // Marginally undecided, but conditionally definite and opposite per context.
    EXPECT_NEAR(store.memory_preference(h, 1), 0.0, 0.1);
    EXPECT_GT(store.conditional_preference(h, 0, false, 1), 0.9);  // ctx0 -> |0>
    EXPECT_LT(store.conditional_preference(h, 0, true, 1), -0.9);  // ctx1 -> |1>

    // Learned policy survives a rearm (memory is outside the coordination).
    const double ctx0 = store.conditional_preference(h, 0, false, 1);
    ASSERT_TRUE(store.rearm(h, 10.0));
    EXPECT_NEAR(store.conditional_preference(h, 0, false, 1), ctx0, 1e-9);

    // Out-of-range memory qubit rejected.
    EXPECT_FALSE(store.reward_pair(h, 0, false, 5, false, 0.5));
    EXPECT_EQ(store.conditional_preference(h, 5, false, 1), 0.0);
}

// Doctrine learning relies on a group hub's memory surviving the per-frame
// reshape() the commander runs as squad size changes -- reshape rebuilds the
// COORDINATION but must leave the (unmeasured) memory register untouched.
TEST(AgentCognition, MemorySurvivesReshape)
{
    AgentCognitionStore store;
    AgentSpec spec;
    spec.agent_count = 1;   // a lone hub to start
    spec.clock = anneal_clock();
    spec.memory_qubits = 1;
    const AgentHandle h = store.create(spec);
    ASSERT_NE(h, kInvalidAgent);

    for (int i = 0; i < 8; ++i) {
        store.reward(h, 0, /*toward=*/true, 0.5);
    }
    const double learned = store.memory_preference(h, 0);
    EXPECT_GT(learned, 0.5);

    // Grow into a 3-qubit star group (hub + 2 members), as the commander does.
    const std::vector<ExactBond> star{ { 0u, 1u, 1.0 }, { 0u, 2u, 1.0 } };
    ASSERT_TRUE(store.reshape(h, 3u, star, 10.0));
    EXPECT_EQ(store.agent_count(h), 3u);
    EXPECT_NEAR(store.memory_preference(h, 0), learned, 1e-9);  // doctrine intact
}

// An agent with no memory register rejects reward / reads back 0 (no crash, no
// silent success).
TEST(AgentCognition, NoMemoryRejectsLearning)
{
    AgentCognitionStore store;
    AgentSpec spec;
    spec.agent_count = 2;
    spec.clock = anneal_clock();
    // memory_qubits left at 0.
    const AgentHandle h = store.create(spec);
    ASSERT_NE(h, kInvalidAgent);

    EXPECT_FALSE(store.reward(h, 0, true, 1.0));
    EXPECT_EQ(store.memory_preference(h, 0), 0.0);
}

// A REJECTED reshape must leave the agent completely intact -- validate before
// mutate. A chi>=2 (TTN) agent accepts only nearest-neighbour chain bonds, so a
// star bond (0-2) trips build_ttn. If reshape resized the bookkeeping BEFORE that
// check, a rejected SHRINK would leave agent_count longer than the shortened
// vectors and the next think() would read past them (heap corruption). Here the
// agent must survive unchanged and stay fully usable.
TEST(AgentCognition, RejectedReshapeLeavesAgentIntact)
{
    AgentCognitionStore store;
    AgentSpec spec;
    spec.agent_count = 4;
    spec.chi = 2;   // TTN backend -- only nearest-neighbour chain bonds are buildable
    spec.clock = anneal_clock();
    spec.bonds = { ExactBond{ 0, 1, 1.0 }, ExactBond{ 1, 2, 1.0 },
        ExactBond{ 2, 3, 1.0 } };
    spec.goals = { Goal{ 0, 0.6 } };
    const AgentHandle h = store.create(spec);
    ASSERT_NE(h, kInvalidAgent);
    ASSERT_EQ(store.agent_count(h), 4u);

    store.start(h, 0.0);
    for (int i = 1; i <= 80; ++i) {
        store.think(h, 0.1 * i);
    }
    ASSERT_GT(store.marginal(h, 0), 0.8);   // committed to its + goal before the reshape

    // Reshape to 3 qubits with a STAR (non-chain) bond 0-2 -- build_ttn rejects it.
    // Shrinking 4 -> 3 is the heap-corruption case if the vectors are resized first.
    const bool ok = store.reshape(
        h, 3u, { ExactBond{ 0, 1, 1.0 }, ExactBond{ 0, 2, 1.0 } }, 8.1);
    EXPECT_FALSE(ok);                      // rejected
    EXPECT_EQ(store.agent_count(h), 4u);   // shape left intact

    // Still fully usable: think() must not read past any vector (under ASan the old
    // code would heap-overflow here), and it stays a healthy 4-qubit agent.
    for (int i = 1; i <= 10; ++i) {
        store.think(h, 8.1 + 0.1 * i);
    }
    const double m3 = store.marginal(h, 3);   // the 4th qubit still exists
    EXPECT_GE(m3, -1.0001);
    EXPECT_LE(m3, 1.0001);
    EXPECT_GT(store.marginal(h, 0), 0.8);     // still holds its goal -- fully alive
}

// rearm() re-opens every decision to a fresh equal superposition, so BOTH reads
// must reflect that immediately: committed() = deliberating AND marginal() ~ 0.
// The bug left the pre-rearm +/-1 in the marginal cache, so marginal() reported a
// maximally-wrong "live" value until the next think() even though committed()
// already said deliberating.
TEST(AgentCognition, RearmResetsStaleMarginal)
{
    AgentCognitionStore store;
    AgentSpec spec;
    spec.agent_count = 2;
    spec.clock = anneal_clock();
    spec.goals = { Goal{ 0, 0.8 } };
    const AgentHandle h = store.create(spec);
    ASSERT_NE(h, kInvalidAgent);

    run_anneal(store, h);   // start + anneal -> committed, marginal polarized to +z
    ASSERT_TRUE(store.committed(h, 0).has_value());
    ASSERT_GT(store.marginal(h, 0), 0.8);

    ASSERT_TRUE(store.rearm(h, 8.1));
    EXPECT_FALSE(store.committed(h, 0).has_value());  // deliberating
    EXPECT_NEAR(store.marginal(h, 0), 0.0, 1e-9);     // reset, NOT the stale ~1
}
