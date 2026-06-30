#include <engine/cognition/exclusivity.h>

#include <gtest/gtest.h>

#include <cmath>

using namespace wz::cognition;

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
