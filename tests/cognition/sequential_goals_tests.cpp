#include <cognition/sequential_goals.h>

#include <cognition/exact_group.h>
#include <cognition/qstate/qstate.h>

#include <gtest/gtest.h>

#include <vector>

using namespace wz::engine::cognition;

TEST(SequentialGoals, PhaseAdvancesAndWraps)
{
    wz::engine::cognition::qstate::Register phase = make_phase_register(2);  // 4 phases
    EXPECT_EQ(current_phase(phase), 0u);
    advance_phase(phase);
    EXPECT_EQ(current_phase(phase), 1u);
    advance_phase(phase);
    EXPECT_EQ(current_phase(phase), 2u);
    advance_phase(phase);
    EXPECT_EQ(current_phase(phase), 3u);
    advance_phase(phase);
    EXPECT_EQ(current_phase(phase), 0u);  // wraps
}

// The decision tracks the active objective as the phase steps through the plan:
// phase 0 pursues +z, phase 1 -z, phase 2 +z -- the agent works the sequence.
TEST(SequentialGoals, DecisionFollowsTheStagedPlan)
{
    const std::vector<Stage> plan = {
        Stage{ .phase = 0, .qubit = 0, .field = 0.5 },
        Stage{ .phase = 1, .qubit = 0, .field = -0.5 },
        Stage{ .phase = 2, .qubit = 0, .field = 0.5 },
    };
    wz::engine::cognition::qstate::Register phase = make_phase_register(2);

    const double expected_sign[3] = { +1.0, -1.0, +1.0 };
    for (int stage_i = 0; stage_i < 3; ++stage_i) {
        ExactGroup decision = make_exact_group(1, {});
        set_goals(decision, active_goals(plan, current_phase(phase)));
        relax(decision, 0.1, 0.05, 300);

        const double z = decision_z(decision, 0);
        EXPECT_GT(z * expected_sign[stage_i], 0.8)
            << "stage " << stage_i << " z=" << z;

        advance_phase(phase);  // objective complete -> next stage
    }
}

// A single stage can carry several goals (an objective spanning multiple
// dispositions): both decision qubits commit in their stage.
TEST(SequentialGoals, AStageCanDriveMultipleDispositions)
{
    const std::vector<Stage> plan = {
        Stage{ .phase = 0, .qubit = 0, .field = 0.5 },
        Stage{ .phase = 0, .qubit = 1, .field = -0.5 },
        Stage{ .phase = 1, .qubit = 0, .field = -0.5 },
    };
    wz::engine::cognition::qstate::Register phase = make_phase_register(1);  // 2 phases

    const auto goals0 = active_goals(plan, current_phase(phase));
    ASSERT_EQ(goals0.size(), 2u);
    ExactGroup d0 = make_exact_group(2, {});
    set_goals(d0, goals0);
    relax(d0, 0.1, 0.05, 300);
    EXPECT_GT(decision_z(d0, 0), 0.8);
    EXPECT_LT(decision_z(d0, 1), -0.8);

    advance_phase(phase);
    const auto goals1 = active_goals(plan, current_phase(phase));
    ASSERT_EQ(goals1.size(), 1u);
    EXPECT_EQ(goals1[0].agent, 0u);
}
