#include <cognition/exclusivity.h>

#include <gtest/gtest.h>

#include <cmath>

using namespace wz::engine::cognition;

namespace
{
    // "Active" probability of a disposition = (1 - <sigma_z>)/2 (|1> = active).
    double active(const ExactGroup& g, uint32_t qubit)
    {
        return 0.5 * (1.0 - decision_z(g, qubit));
    }
}

// Soft one-hot over three dispositions: exactly one is active. Without a
// tiebreaker the choice is the symmetric superposition of the three one-hot
// options -- each active with weight ~1/3, summing to ~1.
TEST(Exclusivity, ThreeWayChoosesExactlyOne)
{
    const AgentLayout layout = make_agent_layout({ 3 });
    ExactGroup g = make_exact_group(layout.total_qubits, {});
    add_one_hot(g, layout, /*agent=*/0, /*strength=*/2.0);
    relax(g, /*gamma=*/0.1, /*dtau=*/0.05, /*iterations=*/500);

    const double n0 = active(g, qubit_of(layout, 0, 0));
    const double n1 = active(g, qubit_of(layout, 0, 1));
    const double n2 = active(g, qubit_of(layout, 0, 2));

    EXPECT_NEAR(n0 + n1 + n2, 1.0, 0.2);  // exactly one active
    EXPECT_NEAR(n0, 1.0 / 3.0, 0.15);     // symmetric among the three
    EXPECT_NEAR(n1, 1.0 / 3.0, 0.15);
    EXPECT_NEAR(n2, 1.0 / 3.0, 0.15);
}

// A tiebreaker goal selects which of the mutually-exclusive dispositions wins:
// the favored one becomes active and the others switch off, still summing to ~1.
TEST(Exclusivity, TiebreakerSelectsADisposition)
{
    const AgentLayout layout = make_agent_layout({ 3 });
    ExactGroup g = make_exact_group(layout.total_qubits, {});
    add_one_hot(g, layout, 0, 2.0);
    // Bias disposition 1 toward active (|1> = -z) -- a negative field.
    g.goal_field[qubit_of(layout, 0, 1)] -= 1.0;
    relax(g, 0.1, 0.05, 500);

    EXPECT_GT(active(g, qubit_of(layout, 0, 1)), 0.8);   // the chosen one
    EXPECT_LT(active(g, qubit_of(layout, 0, 0)), 0.2);
    EXPECT_LT(active(g, qubit_of(layout, 0, 2)), 0.2);
}

// The binary case reduces to a single antiferromagnetic bond: exactly one of the
// two dispositions is active.
TEST(Exclusivity, BinaryIsMutuallyExclusive)
{
    const AgentLayout layout = make_agent_layout({ 2 });
    ExactGroup g = make_exact_group(layout.total_qubits, {});
    add_one_hot(g, layout, 0, 2.0);
    relax(g, 0.1, 0.05, 400);

    const double n0 = active(g, qubit_of(layout, 0, 0));
    const double n1 = active(g, qubit_of(layout, 0, 1));
    EXPECT_NEAR(n0 + n1, 1.0, 0.15);
    EXPECT_LT(connected_correlation(g, qubit_of(layout, 0, 0),
                  qubit_of(layout, 0, 1)),
        -0.8);  // anti-correlated
}

// A layout WIDER than the group it is applied to must not smuggle an
// out-of-range bond past make_exact_group's endpoint guard: add_one_hot appends
// AFTER that guard has run, and relax_step applies each bond's ZZ unchecked, so
// an endpoint the joint register does not have is a phantom single-site field at
// best and shift UB at >= 64. Terms naming a missing qubit are dropped; the ones
// that fit still land.
TEST(Exclusivity, TermsOutsideTheGroupAreDropped)
{
    const AgentLayout layout = make_agent_layout({ 3 });
    ExactGroup g = make_exact_group(/*agent_count=*/2, {});   // narrower than the layout

    add_one_hot(g, layout, /*agent=*/0, /*strength=*/2.0);

    // Only the (0,1) pair is representable; every bond touching qubit 2 is gone.
    EXPECT_EQ(g.bonds.size(), 1u);
    for (const ExactBond& b : g.bonds) {
        EXPECT_LT(b.a, g.joint.qubits);
        EXPECT_LT(b.b, g.joint.qubits);
    }
    // Dropping is a SAFETY fallback, not a repair: what survives is the 3-way
    // penalty with a term missing (each remaining qubit still carries the k = 3
    // inactive bias h = A*(k-2)/2), so it is not a valid 2-way one-hot and the
    // group leans inactive. The guarantee is only that relaxation is well
    // defined -- finite marginals in range, no phantom field from a qubit that
    // is not there. Match the layout to the group to get meaningful exclusivity.
    relax(g, 0.1, 0.05, 400);
    for (uint32_t q = 0; q < g.joint.qubits; ++q) {
        const double z = decision_z(g, q);
        EXPECT_TRUE(std::isfinite(z));
        EXPECT_GE(z, -1.0);
        EXPECT_LE(z, 1.0);
    }
}

// An agent the layout does not describe is a no-op, not an out-of-bounds read.
TEST(Exclusivity, UnknownAgentIsANoOp)
{
    const AgentLayout layout = make_agent_layout({ 2 });
    ExactGroup g = make_exact_group(layout.total_qubits, {});

    add_one_hot(g, layout, /*agent=*/7, /*strength=*/2.0);

    EXPECT_TRUE(g.bonds.empty());
    for (const double h : g.goal_field) {
        EXPECT_DOUBLE_EQ(h, 0.0);
    }
}
