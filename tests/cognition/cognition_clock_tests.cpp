#include <cognition/cognition_clock.h>

#include <cognition/coordination.h>
#include <cognition/exact_group.h>
#include <cognition/ttn.h>

#include <gtest/gtest.h>

#include <cmath>
#include <utility>
#include <vector>

using namespace wz::engine::cognition;

namespace
{
    CognitionClock make_clock()
    {
        CognitionClock clock;
        clock.gamma_start = 3.0;
        clock.gamma_end = 0.0;
        clock.anneal_seconds = 4.0;
        clock.relax_rate = 1.0;
        clock.max_substep = 0.05;
        return clock;
    }

    Coordination make_goal_pair()
    {
        ExactGroup g = make_exact_group(2, { ExactBond{ 0, 1, 1.0 } });
        set_goals(g, { Goal{ .agent = 0, .field = 0.5 } });
        return Coordination{ std::move(g) };
    }
}

namespace
{
    // Drive a fresh goal-pair through the given sequence of wake sim-times (clock
    // zeroed at 0.0, anneal sweep over [0, 4]) and return agent 0's decision.
    double drive(const std::vector<double>& wakes, double max_substep)
    {
        Coordination c = make_goal_pair();
        CognitionClock clock = make_clock();  // gamma 3 -> 0 over [0, 4]
        clock.max_substep = max_substep;
        start(clock, 0.0);
        for (double t : wakes) {
            tick(c, clock, t);
        }
        return decision_z(c, 0);
    }

    std::vector<double> even_wakes(double step, double end)
    {
        std::vector<double> wakes;
        for (double t = step; t <= end + 1e-9; t += step) {
            wakes.push_back(t);
        }
        return wakes;
    }
}

// The headline property: the decision is a function of sim-time, not of how the
// self-paced wakes landed. An agent woken 80 times across the anneal reaches
// essentially the same mental state as one woken 8 times over the same span,
// because a long step is substepped (and Gamma sampled) to match many short ones.
// This is convergence to the shared continuous-time trajectory, not bit-identity --
// the substep grids differ slightly -- so they agree to the discretization scale.
TEST(CognitionClock, CadenceInvariance)
{
    const double fine = drive(even_wakes(0.05, 4.0), /*max_substep=*/0.05);
    const double coarse = drive(even_wakes(0.5, 4.0), /*max_substep=*/0.05);
    EXPECT_NEAR(fine, coarse, 1e-2);
}

// Substepping's load-bearing role: an agent that SLEEPS through a chunk of its
// anneal still experiences the Gamma ramp. A single wake at t = 4 with substepping
// ramps Gamma 3 -> 0 across the sleep and lands where the finely-woken agent does;
// without substepping it takes one frozen-Gamma imaginary-time step (Gamma at the
// sleep's midpoint) and commits to the wrong Hamiltonian's ground state.
TEST(CognitionClock, SubsteppingHonorsTheSweepAcrossASleep)
{
    const double fine = drive(even_wakes(0.05, 4.0), /*max_substep=*/0.05);
    const double slept_substepped = drive({ 4.0 }, /*max_substep=*/0.05);
    const double slept_frozen = drive({ 4.0 }, /*max_substep=*/0.0);

    // Substepping the long sleep tracks the fine trajectory...
    EXPECT_NEAR(fine, slept_substepped, 1e-2);
    // ...while a single frozen-Gamma step lands materially off the committed state.
    EXPECT_GT(std::abs(fine - slept_frozen), 0.05);
    EXPECT_GT(std::abs(fine - slept_frozen), 5.0 * std::abs(fine - slept_substepped));
}

// Gamma sweeps linearly across the anneal window and clamps to the endpoints
// outside it.
TEST(CognitionClock, GammaInterpolatesAndClamps)
{
    CognitionClock clock = make_clock();  // gamma 3 -> 0 over [0, 4]
    start(clock, 10.0);                   // started at sim-time 10

    EXPECT_NEAR(gamma_at_time(clock, 9.0), 3.0, 1e-12);   // before -> start
    EXPECT_NEAR(gamma_at_time(clock, 10.0), 3.0, 1e-12);  // at start
    EXPECT_NEAR(gamma_at_time(clock, 12.0), 1.5, 1e-12);  // midpoint -> mean
    EXPECT_NEAR(gamma_at_time(clock, 14.0), 0.0, 1e-12);  // at end
    EXPECT_NEAR(gamma_at_time(clock, 99.0), 0.0, 1e-12);  // after -> end
}

// anneal_seconds == 0 means "no sweep": Gamma is pinned at gamma_end.
TEST(CognitionClock, ZeroSweepHoldsGammaEnd)
{
    CognitionClock clock;
    clock.gamma_start = 5.0;
    clock.gamma_end = 0.25;
    clock.anneal_seconds = 0.0;
    start(clock, 0.0);

    EXPECT_NEAR(gamma_at_time(clock, 0.0), 0.25, 1e-12);
    EXPECT_NEAR(gamma_at_time(clock, 100.0), 0.25, 1e-12);
}

// A full time-driven anneal through the Coordination seam commits the group, and
// the goal propagates across the coupling -- the time-driven analog of the discrete
// anneal test.
TEST(CognitionClock, TimeDrivenAnnealCommits)
{
    TtnChain t = make_ttn_chain(3, { 1.0, 1.0 }, /*chi=*/2);
    t.goal_field[0] = 0.5;
    Coordination c = std::move(t);

    CognitionClock clock = make_clock();
    clock.anneal_seconds = 6.0;
    start(clock, 0.0);

    // Wake every 0.1s out to t = 8.0 (past the sweep, into the committed hold).
    for (int i = 1; i <= 80; ++i) {
        tick(c, clock, 0.1 * i);
    }

    EXPECT_GT(decision_z(c, 0), 0.8);
    EXPECT_GT(decision_z(c, 1), 0.8);
    EXPECT_GT(decision_z(c, 2), 0.8);
}

// The first tick only stamps the origin (no elapsed time to relax across), and a
// wake that doesn't advance sim-time does no work.
TEST(CognitionClock, FirstTickAndStaleWakeAreNoOps)
{
    Coordination c = make_goal_pair();
    CognitionClock clock = make_clock();

    const double z_before = decision_z(c, 0);

    // No explicit start(): the first tick lazily zeroes the clock and relaxes nothing.
    EXPECT_EQ(tick(c, clock, 5.0), 0.0);
    EXPECT_TRUE(clock.started);
    EXPECT_EQ(clock.last_tick, 5.0);
    EXPECT_NEAR(decision_z(c, 0), z_before, 1e-12);

    // A wake at or before last_tick advances nothing.
    EXPECT_EQ(tick(c, clock, 5.0), 0.0);
    EXPECT_EQ(tick(c, clock, 4.0), 0.0);
    EXPECT_NEAR(decision_z(c, 0), z_before, 1e-12);
}
