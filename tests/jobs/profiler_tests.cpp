#include <gtest/gtest.h>
#include <vector>

#include <jobs/job_graph_template.h>
#include <jobs/frame_execution.h>
#include <jobs/dag_scheduler.h>
#include <jobs/job_profiler.h>

using namespace wz::jobs;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{
    JobFn noop_fn = [](JobContext&) {};
}

// ---------------------------------------------------------------------------
// FrameJobProfile — unit tests
// ---------------------------------------------------------------------------

TEST(FrameJobProfile, ResetClearsPreviousFrame)
{
    FrameJobProfile profile;
    profile.reset(1);
    profile.record(NodeHandle{0}, "A", 100, 200);
    EXPECT_EQ(profile.timings.size(), 1u);

    profile.reset(2);
    EXPECT_EQ(profile.frame_index, 2u);
    EXPECT_TRUE(profile.timings.empty());
}

// ---------------------------------------------------------------------------
// DagSchedulerProfiler — scheduler integration tests
// ---------------------------------------------------------------------------

TEST(DagSchedulerProfiler, DisabledByDefaultDoesNotRequireProfile)
{
    // Existing behavior must be unaffected when no profile is set.
    JobGraphTemplate tmpl;
    auto a = tmpl.add_job({ .name = "A", .run = noop_fn });
    auto b = tmpl.add_job({ .name = "B", .run = noop_fn });
    tmpl.add_dependency(a, b);
    ASSERT_TRUE(tmpl.commit());

    FrameExecution exec;
    exec.reset(tmpl);

    DagScheduler sched;  // profile_ is nullptr by default
    sched.execute(tmpl, exec);

    EXPECT_EQ(exec.status[a], JobStatus::Done);
    EXPECT_EQ(exec.status[b], JobStatus::Done);
    EXPECT_EQ(exec.remaining_jobs, 0u);
}

TEST(DagSchedulerProfiler, RecordsJobsInExecutionOrder)
{
    // A -> B -> C: profile must record 3 entries in A, B, C order.
    JobGraphTemplate tmpl;
    auto a = tmpl.add_job({ .name = "A", .run = noop_fn });
    auto b = tmpl.add_job({ .name = "B", .run = noop_fn });
    auto c = tmpl.add_job({ .name = "C", .run = noop_fn });
    tmpl.add_dependency(a, b);
    tmpl.add_dependency(b, c);
    ASSERT_TRUE(tmpl.commit());

    FrameExecution exec;
    exec.reset(tmpl);

    FrameJobProfile profile;
    profile.reset(42);

    DagScheduler sched;
    sched.set_profile(&profile);
    sched.execute(tmpl, exec);
    sched.set_profile(nullptr);

    ASSERT_EQ(profile.timings.size(), 3u);

    EXPECT_EQ(profile.timings[0].node, a);
    EXPECT_STREQ(profile.timings[0].name, "A");
    EXPECT_GE(profile.timings[0].end_ticks, profile.timings[0].start_ticks);

    EXPECT_EQ(profile.timings[1].node, b);
    EXPECT_STREQ(profile.timings[1].name, "B");
    EXPECT_GE(profile.timings[1].end_ticks, profile.timings[1].start_ticks);

    EXPECT_EQ(profile.timings[2].node, c);
    EXPECT_STREQ(profile.timings[2].name, "C");
    EXPECT_GE(profile.timings[2].end_ticks, profile.timings[2].start_ticks);

    // All nodes must be Done.
    EXPECT_EQ(exec.status[a], JobStatus::Done);
    EXPECT_EQ(exec.status[b], JobStatus::Done);
    EXPECT_EQ(exec.status[c], JobStatus::Done);
}

TEST(DagSchedulerProfiler, RecordsNullRunJobsConsistently)
{
    // Jobs with run == nullptr are still executed (no-op) and must appear in
    // the profile so record counts match node counts.
    JobGraphTemplate tmpl;
    auto a = tmpl.add_job({ .name = "null_job", .run = nullptr });
    auto b = tmpl.add_job({ .name = "real_job", .run = noop_fn });
    tmpl.add_dependency(a, b);
    ASSERT_TRUE(tmpl.commit());

    FrameExecution exec;
    exec.reset(tmpl);

    FrameJobProfile profile;
    profile.reset(0);

    DagScheduler sched;
    sched.set_profile(&profile);
    sched.execute(tmpl, exec);
    sched.set_profile(nullptr);

    ASSERT_EQ(profile.timings.size(), 2u);
    EXPECT_EQ(profile.timings[0].node, a);
    EXPECT_GE(profile.timings[0].end_ticks, profile.timings[0].start_ticks);
    EXPECT_EQ(profile.timings[1].node, b);

    EXPECT_EQ(exec.status[a], JobStatus::Done);
    EXPECT_EQ(exec.status[b], JobStatus::Done);
}

TEST(DagSchedulerProfiler, ProfileDoesNotChangeSchedulingBehavior)
{
    // Verify that enabling the profiler produces the same execution order
    // as running without a profile.
    std::vector<NodeHandle> order_without;
    std::vector<NodeHandle> order_with;

    JobFn record_fn = [](JobContext& ctx) {
        static_cast<std::vector<NodeHandle>*>(ctx.frame_user)->push_back(ctx.node);
    };

    auto build = [&](std::vector<NodeHandle>& out) {
        JobGraphTemplate tmpl;
        auto a = tmpl.add_job({ .name = "A", .run = record_fn });
        auto b = tmpl.add_job({ .name = "B", .run = record_fn });
        auto c = tmpl.add_job({ .name = "C", .run = record_fn });
        tmpl.add_dependency(a, b);
        tmpl.add_dependency(b, c);
        EXPECT_TRUE(tmpl.commit());

        FrameExecution exec;
        exec.reset(tmpl);
        for (uint32_t i = 0; i < tmpl.node_count(); ++i)
            exec.bind(static_cast<NodeHandle>(i), &out);
        return std::make_pair(std::move(tmpl), std::move(exec));
    };

    {
        auto [tmpl, exec] = build(order_without);
        DagScheduler sched;
        sched.execute(tmpl, exec);
    }

    {
        auto [tmpl, exec] = build(order_with);
        FrameJobProfile profile;
        profile.reset(0);
        DagScheduler sched;
        sched.set_profile(&profile);
        sched.execute(tmpl, exec);
    }

    ASSERT_EQ(order_without.size(), order_with.size());
    for (size_t i = 0; i < order_without.size(); ++i)
        EXPECT_EQ(order_without[i], order_with[i]);
}
