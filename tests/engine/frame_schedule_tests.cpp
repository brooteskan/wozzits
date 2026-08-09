// tests/engine/frame_schedule_tests.cpp
//
// Unit coverage for the per-frame phase DAG. The live host loop that drives it
// needs a GPU device and is not unit-tested, so this component carries the logic
// coverage: phase order, error short-circuit, unset-phase handling, and the
// profiling -> critical-path wiring. Phases are mock callbacks.

#include <gtest/gtest.h>

#include <functional>
#include <vector>

#include <engine/frame_schedule.h>

using namespace wz::engine;

namespace
{
    // Ops whose every phase records itself into `order` and returns true.
    FramePhaseOps recording_ops(std::vector<FramePhase>& order)
    {
        auto rec = [&order](FramePhase p) {
            return [&order, p]() { order.push_back(p); return true; };
        };
        FramePhaseOps ops;
        ops.simulate       = rec(FramePhase::Simulate);
        ops.begin_frame    = rec(FramePhase::BeginFrame);
        ops.clear          = rec(FramePhase::Clear);
        ops.render_scene   = rec(FramePhase::RenderScene);
        ops.render_overlay = rec(FramePhase::RenderOverlay);
        ops.end_frame      = rec(FramePhase::EndFrame);
        ops.present        = rec(FramePhase::Present);
        return ops;
    }
}

TEST(FrameSchedule, RunsAllPhasesInOrder)
{
    std::vector<FramePhase> order;
    FramePhaseOps ops = recording_ops(order);

    FrameSchedule schedule;
    const FrameSchedule::Result r = schedule.run(ops);

    EXPECT_TRUE(r.ok);
    ASSERT_EQ(order.size(), 7u);
    EXPECT_EQ(order[0], FramePhase::Simulate);
    EXPECT_EQ(order[1], FramePhase::BeginFrame);
    EXPECT_EQ(order[2], FramePhase::Clear);
    EXPECT_EQ(order[3], FramePhase::RenderScene);
    EXPECT_EQ(order[4], FramePhase::RenderOverlay);
    EXPECT_EQ(order[5], FramePhase::EndFrame);
    EXPECT_EQ(order[6], FramePhase::Present);
}

TEST(FrameSchedule, StopsAtFirstFailingPhase)
{
    std::vector<FramePhase> order;
    FramePhaseOps ops = recording_ops(order);

    // Clear fails: the frame must stop there, and render_scene onward must not run.
    ops.clear = [&order]() { order.push_back(FramePhase::Clear); return false; };

    FrameSchedule schedule;
    const FrameSchedule::Result r = schedule.run(ops);

    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.failed_phase, FramePhase::Clear);
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], FramePhase::Simulate);
    EXPECT_EQ(order[1], FramePhase::BeginFrame);
    EXPECT_EQ(order[2], FramePhase::Clear);
}

TEST(FrameSchedule, UnsetPhaseIsSkippedAndSucceeds)
{
    std::vector<FramePhase> order;
    FramePhaseOps ops = recording_ops(order);
    ops.render_overlay = {};   // no overlay this frame

    FrameSchedule schedule;
    const FrameSchedule::Result r = schedule.run(ops);

    EXPECT_TRUE(r.ok);
    ASSERT_EQ(order.size(), 6u);
    // RenderOverlay is absent; the rest keep their order and Present still runs.
    EXPECT_EQ(order[3], FramePhase::RenderScene);
    EXPECT_EQ(order[4], FramePhase::EndFrame);
    EXPECT_EQ(order[5], FramePhase::Present);
}

TEST(FrameSchedule, ProfilingRecordsEveryPhaseAndAnalyzes)
{
    std::vector<FramePhase> order;
    FramePhaseOps ops = recording_ops(order);

    FrameSchedule schedule;
    schedule.set_profiling(true);
    const FrameSchedule::Result r = schedule.run(ops);

    EXPECT_TRUE(r.ok);
    // One timing record per phase, and the critical-path analysis ran over them.
    EXPECT_EQ(schedule.last_profile().timings.size(), 7u);
    EXPECT_FALSE(schedule.last_analysis().path.empty());
    EXPECT_GE(schedule.last_analysis().parallelism(), 1.0);  // serial chain
}

TEST(FrameSchedule, ProfilingDisabledRecordsNothing)
{
    std::vector<FramePhase> order;
    FramePhaseOps ops = recording_ops(order);

    FrameSchedule schedule;   // profiling off by default
    schedule.run(ops);

    EXPECT_TRUE(schedule.last_profile().timings.empty());
}

TEST(FrameSchedule, ReusableAcrossFrames)
{
    FrameSchedule schedule;

    std::vector<FramePhase> a, b;
    FramePhaseOps ops_a = recording_ops(a);
    FramePhaseOps ops_b = recording_ops(b);

    EXPECT_TRUE(schedule.run(ops_a).ok);
    EXPECT_TRUE(schedule.run(ops_b).ok);
    EXPECT_EQ(a.size(), 7u);
    EXPECT_EQ(b.size(), 7u);
}
