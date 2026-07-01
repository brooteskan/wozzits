#include <cognition/learning.h>

#include <cognition/exact_group.h>
#include <cognition/qstate/qstate.h>

#include <gtest/gtest.h>

#include <complex>

using namespace wz::engine::cognition;
using wz::engine::cognition::qstate::Register;

namespace
{
    // Probability of a basis state of the memory register.
    double prob(const Register& reg, uint64_t basis)
    {
        return std::norm(reg.amp[basis]);
    }
}

// Repeated reward concentrates the memory on the rewarded branch -- a monotonic
// learning curve toward probability 1.
TEST(Learning, RewardConcentratesMemory)
{
    Register memory = wz::engine::cognition::qstate::uniform(2);  // four branches, 0.25 each
    double last = prob(memory, 0b11);
    for (int i = 0; i < 40; ++i) {
        reward(memory, /*mask=*/0b11, /*match=*/0b11, /*strength=*/0.5);
        const double p = prob(memory, 0b11);
        EXPECT_GE(p, last - 1e-12);  // monotonic
        last = p;
    }
    EXPECT_GT(prob(memory, 0b11), 0.99);
    EXPECT_NEAR(wz::engine::cognition::qstate::norm(memory), 1.0, 1e-9);
}

// Switching which branch is rewarded re-concentrates the memory: the agent
// unlearns the old preference and learns the new one.
TEST(Learning, SwitchingRewardRelearns)
{
    Register memory = wz::engine::cognition::qstate::uniform(2);
    for (int i = 0; i < 40; ++i) {
        reward(memory, 0b11, 0b11, 0.5);  // learn |11>
    }
    ASSERT_GT(prob(memory, 0b11), 0.99);

    for (int i = 0; i < 80; ++i) {
        reward(memory, 0b11, 0b00, 0.5);  // now reward |00>
    }
    EXPECT_GT(prob(memory, 0b00), 0.95);
    EXPECT_LT(prob(memory, 0b11), 0.05);
}

// The memory is never measured, so a decision collapse leaves it intact: after
// learning, measuring the (product) decision qubit does not disturb the memory.
TEST(Learning, MemoryPersistsAcrossDecisionMeasurement)
{
    // qubit 0 = decision (stays |+>), qubit 1 = memory.
    Register reg = wz::engine::cognition::qstate::uniform(2);
    for (int i = 0; i < 20; ++i) {
        reward(reg, /*mask=*/0b10, /*match=*/0b10, 0.4);  // bias memory bit 1 -> 1
    }
    const double mem_before = memory_preference(reg, 1);
    ASSERT_LT(mem_before, -0.5);  // learned toward |1> (<sigma_z> negative)

    wz::engine::cognition::qstate::Rng rng{ 5u };
    (void)wz::engine::cognition::qstate::measure(reg, 0, rng);  // commit the decision qubit only
    EXPECT_NEAR(memory_preference(reg, 1), mem_before, 1e-9);  // memory intact
}

// The full loop: a learned memory biases the decision. Feeding the memory's
// preference as a goal field makes the decision commit the way the agent learned
// -- and relearning the memory flips the decision.
TEST(Learning, MemoryBiasesTheDecision)
{
    auto decide_from_memory = [](const Register& memory) {
        const double pref = memory_preference(memory, 0);  // [-1, 1]
        ExactGroup decision = make_exact_group(1, {});
        set_goals(decision, { Goal{ .agent = 0, .field = pref } });
        relax(decision, 0.1, 0.05, 300);
        return decision_z(decision, 0);
    };

    Register memory = wz::engine::cognition::qstate::uniform(1);
    for (int i = 0; i < 20; ++i) {
        reward(memory, /*mask=*/0b1, /*match=*/0b0, 0.4);  // prefer |0> (+z)
    }
    EXPECT_GT(decide_from_memory(memory), 0.9);  // decision commits +z

    for (int i = 0; i < 40; ++i) {
        reward(memory, 0b1, 0b1, 0.4);  // relearn: prefer |1> (-z)
    }
    EXPECT_LT(decide_from_memory(memory), -0.9);  // decision flips
}
