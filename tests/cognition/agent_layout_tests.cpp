#include <cognition/agent_layout.h>

#include <cognition/exact_group.h>

#include <gtest/gtest.h>

#include <cmath>

using namespace wz::engine::cognition;

TEST(AgentLayout, AddressesDispositionsToQubits)
{
    const AgentLayout layout = make_agent_layout({ 2, 3, 1 });
    EXPECT_EQ(layout.total_qubits, 6u);
    EXPECT_EQ(agent_count(layout), 3u);
    EXPECT_EQ(qubit_of(layout, 0, 0), 0u);
    EXPECT_EQ(qubit_of(layout, 0, 1), 1u);
    EXPECT_EQ(qubit_of(layout, 1, 0), 2u);  // agent 1 starts after agent 0's two
    EXPECT_EQ(qubit_of(layout, 1, 2), 4u);
    EXPECT_EQ(qubit_of(layout, 2, 0), 5u);

    // Out of range is kInvalidQubit, not a silent neighbour's qubit or an
    // out-of-bounds read: a layout can be wider than the group addressing it.
    EXPECT_EQ(qubit_of(layout, 0, 2), kInvalidQubit);   // agent 0 has only 2
    EXPECT_EQ(qubit_of(layout, 2, 1), kInvalidQubit);   // agent 2 has only 1
    EXPECT_EQ(qubit_of(layout, 3, 0), kInvalidQubit);   // no agent 3
}

// One agent, two independent dispositions (e.g. flee?, alert?), each biased by
// its own goal: they commit independently in opposite directions.
TEST(AgentLayout, MultiDispositionGoalsAreIndependent)
{
    const AgentLayout layout = make_agent_layout({ 2 });  // agent 0: flee, alert
    const uint32_t flee = qubit_of(layout, 0, 0);
    const uint32_t alert = qubit_of(layout, 0, 1);

    ExactGroup g = make_exact_group(layout.total_qubits, {});
    set_goals(g, { Goal{ .agent = flee, .field = 0.5 },
        Goal{ .agent = alert, .field = -0.5 } });
    relax(g, 0.1, 0.05, 300);

    EXPECT_GT(decision_z(g, flee), 0.9);
    EXPECT_LT(decision_z(g, alert), -0.9);
}

// Intra-agent CONSISTENCY: coupling two of an agent's dispositions
// ferromagnetically links them ("if fleeing, then alert") -- they correlate.
TEST(AgentLayout, IntraAgentConsistencyCouplingCorrelatesDispositions)
{
    const AgentLayout layout = make_agent_layout({ 2 });
    const uint32_t flee = qubit_of(layout, 0, 0);
    const uint32_t alert = qubit_of(layout, 0, 1);

    ExactGroup g = make_exact_group(
        layout.total_qubits, { ExactBond{ flee, alert, 1.0 } });
    relax(g, 0.1, 0.05, 400);
    EXPECT_GT(connected_correlation(g, flee, alert), 0.9);
}

// Intra-agent EXCLUSIVITY (soft one-hot, binary case): antiferromagnetic
// coupling makes the two dispositions mutually exclusive -- exactly one active.
TEST(AgentLayout, IntraAgentExclusivityAnticorrelatesDispositions)
{
    const AgentLayout layout = make_agent_layout({ 2 });
    const uint32_t a = qubit_of(layout, 0, 0);
    const uint32_t b = qubit_of(layout, 0, 1);

    ExactGroup g = make_exact_group(
        layout.total_qubits, { ExactBond{ a, b, -1.0 } });
    relax(g, 0.1, 0.05, 400);
    EXPECT_LT(connected_correlation(g, a, b), -0.9);
}

// Inter-agent bond between SPECIFIC dispositions: coupling agent 0's "flee" to
// agent 1's "flee" correlates those, while the uncoupled "alert" dispositions
// stay independent.
TEST(AgentLayout, InterAgentBondCouplesSpecificDispositions)
{
    const AgentLayout layout = make_agent_layout({ 2, 2 });
    const uint32_t flee0 = qubit_of(layout, 0, 0);
    const uint32_t flee1 = qubit_of(layout, 1, 0);
    const uint32_t alert0 = qubit_of(layout, 0, 1);
    const uint32_t alert1 = qubit_of(layout, 1, 1);

    ExactGroup g = make_exact_group(
        layout.total_qubits, { ExactBond{ flee0, flee1, 1.0 } });
    relax(g, 0.1, 0.05, 400);

    EXPECT_GT(connected_correlation(g, flee0, flee1), 0.9);     // coupled
    EXPECT_LT(std::abs(connected_correlation(g, alert0, alert1)), 0.2);  // free
}
