// The mind IR (schema "wozzits.mind.ir.v0") is the data-driven counterpart to the
// quantum_agent behavior's scalar config: a mind authored as an explicit GRAPH --
// decision qubits (nodes), per-qubit goal biases, and the couplings (bonds/edges)
// between them, plus backend / anneal / commit / memory. These pin the parser ->
// AgentSpec mapping + its validation, and that the parsed spec actually builds a
// wave function through the AgentCognitionStore.

#include <engine/behavior/mind_ir.h>
#include <engine/behavior/quantum_agent_behaviors.h>
#include <cognition/agent_cognition.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
    namespace cog = wz::engine::cognition;
    using wz::engine::behavior::parse_mind;

    constexpr const char* kGraphMind =
        R"({"schema":"wozzits.mind.ir.v0",)"
        R"("qubits":3,)"
        R"("goals":[{"q":0,"field":0.4},{"q":2,"field":-0.25}],)"
        R"("bonds":[{"a":0,"b":1,"j":-0.8},{"a":0,"b":2,"j":0.5}],)"
        R"("chi":1,"memory":2,)"
        R"("clock":{"gamma_start":3.0,"anneal_seconds":6.0,"relax_rate":1.5},)"
        R"("commit":{"confidence":0.7,"decoherence":0.2}})";
}

TEST(MindIr, ParsesTheGraphIntoAnAgentSpec)
{
    cog::AgentSpec spec;
    std::string err;
    ASSERT_TRUE(parse_mind(kGraphMind, spec, err)) << err;

    EXPECT_EQ(spec.agent_count, 3u);

    ASSERT_EQ(spec.goals.size(), 2u);
    EXPECT_EQ(spec.goals[0].agent, 0u);
    EXPECT_DOUBLE_EQ(spec.goals[0].field, 0.4);
    EXPECT_EQ(spec.goals[1].agent, 2u);
    EXPECT_DOUBLE_EQ(spec.goals[1].field, -0.25);

    ASSERT_EQ(spec.bonds.size(), 2u);
    EXPECT_EQ(spec.bonds[0].a, 0u);
    EXPECT_EQ(spec.bonds[0].b, 1u);
    EXPECT_DOUBLE_EQ(spec.bonds[0].j, -0.8);
    EXPECT_EQ(spec.bonds[1].b, 2u);
    EXPECT_DOUBLE_EQ(spec.bonds[1].j, 0.5);

    EXPECT_EQ(spec.chi, 1u);
    EXPECT_EQ(spec.memory_qubits, 2u);
    EXPECT_DOUBLE_EQ(spec.clock.gamma_start, 3.0);
    EXPECT_DOUBLE_EQ(spec.clock.anneal_seconds, 6.0);
    EXPECT_DOUBLE_EQ(spec.clock.relax_rate, 1.5);
    EXPECT_DOUBLE_EQ(spec.commit.confidence, 0.7);
    EXPECT_DOUBLE_EQ(spec.commit.decoherence_rate, 0.2);
}

TEST(MindIr, DefaultsApplyWhenOptionalBlocksAreAbsent)
{
    cog::AgentSpec spec;
    std::string err;
    ASSERT_TRUE(parse_mind(R"({"qubits":1})", spec, err)) << err;
    EXPECT_EQ(spec.agent_count, 1u);
    EXPECT_TRUE(spec.goals.empty());
    EXPECT_TRUE(spec.bonds.empty());
    EXPECT_EQ(spec.chi, 0u);
    EXPECT_EQ(spec.memory_qubits, 0u);
    EXPECT_DOUBLE_EQ(spec.clock.gamma_start, 2.0);
    EXPECT_DOUBLE_EQ(spec.clock.anneal_seconds, 4.0);
    EXPECT_DOUBLE_EQ(spec.commit.confidence, 0.8);

    // gamma_end -- the knob that decides whether the mind has any quantum
    // structure left at commit time -- defaults to the shared quantum_agent
    // authoring value, NOT to the CognitionClock struct zero: an absent gamma_end
    // leaves a RESIDUAL transverse field, so coupled qubits stay correlated and a
    // marginal near zero still means "undecided" rather than "already decided".
    EXPECT_DOUBLE_EQ(
        spec.clock.gamma_end,
        wz::engine::behavior::kQuantumAgentDefaultGammaEnd);
    // Decoherence stays off: it fires mid-anneal against an unpolarized state, so
    // on by default would turn considered decisions into coin flips.
    EXPECT_DOUBLE_EQ(spec.commit.decoherence_rate, 0.0);
}

// A mind that wants to land fully classical says so, and the explicit zero is
// honoured rather than being overwritten by the non-zero default.
TEST(MindIr, ExplicitZeroGammaEndIsHonoured)
{
    cog::AgentSpec spec;
    std::string err;
    ASSERT_TRUE(parse_mind(
        R"({"qubits":1,"clock":{"gamma_end":0.0}})", spec, err)) << err;
    EXPECT_DOUBLE_EQ(spec.clock.gamma_end, 0.0);
}

TEST(MindIr, RejectsMalformedGraphs)
{
    cog::AgentSpec spec;
    std::string err;
    EXPECT_FALSE(parse_mind(R"({"qubits":0})", spec, err));   // qubits must be >= 1
    EXPECT_FALSE(parse_mind(R"({})", spec, err));             // qubits missing
    EXPECT_FALSE(parse_mind(
        R"({"qubits":2,"goals":[{"q":5,"field":1}]})", spec, err));  // goal out of range
    EXPECT_FALSE(parse_mind(
        R"({"qubits":2,"bonds":[{"a":0,"b":9,"j":1}]})", spec, err)); // bond out of range
    EXPECT_FALSE(parse_mind(
        R"({"qubits":2,"bonds":[{"a":1,"b":1,"j":1}]})", spec, err)); // self-bond
    EXPECT_FALSE(parse_mind("not json at all", spec, err));   // garbage
}

TEST(MindIr, ParsedSpecBuildsAWaveFunction)
{
    cog::AgentSpec spec;
    std::string err;
    // chi=0 exact backend + an arbitrary (non-chain) bond graph -> buildable.
    ASSERT_TRUE(parse_mind(
        R"({"qubits":3,"bonds":[{"a":0,"b":1,"j":-0.6},{"a":0,"b":2,"j":0.6}],"chi":0})",
        spec, err)) << err;

    cog::AgentCognitionStore store;
    const cog::AgentHandle h = store.create(spec);
    EXPECT_NE(h, cog::kInvalidAgent);
}
