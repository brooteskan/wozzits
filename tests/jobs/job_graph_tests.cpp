#include <gtest/gtest.h>
#include <algorithm>
#include <unordered_set>
#include <vector>

#include <jobs/job_graph_template.h>
#include <jobs/frame_execution.h>
#include <jobs/dag_scheduler.h>

using namespace wz::jobs;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{
    // Record execution order via frame_user pointer.
    JobFn record_fn = [](JobContext& ctx)
    {
        static_cast<std::vector<NodeHandle>*>(ctx.frame_user)->push_back(ctx.node);
    };

    // Bind the same tracking vector to every node in the graph.
    void bind_all(FrameExecution& exec, const JobGraphTemplate& tmpl,
                  std::vector<NodeHandle>& out)
    {
        for (uint32_t i = 0; i < tmpl.node_count(); ++i)
            exec.bind(static_cast<NodeHandle>(i), &out);
    }
}

// ---------------------------------------------------------------------------
// JobGraphTemplate — static commit tests
// ---------------------------------------------------------------------------

TEST(JobGraphTemplate, CommitsTopoOrderOnce)
{
    // A → B: A must appear before B in topo order
    JobGraphTemplate tmpl;
    auto a = tmpl.add_job({ .run = nullptr });
    auto b = tmpl.add_job({ .run = nullptr });
    tmpl.add_dependency(a, b);

    EXPECT_TRUE(tmpl.commit());
    EXPECT_TRUE(tmpl.committed());

    auto order = tmpl.topo_order();
    ASSERT_EQ(order.size(), 2u);

    auto pos = [&](NodeHandle n) {
        return std::distance(order.begin(),
                             std::find(order.begin(), order.end(), n));
    };
    EXPECT_LT(pos(a), pos(b));
}

TEST(JobGraphTemplate, ComputesIndegrees)
{
    // A → C, B → C  (C has indegree 2; A and B have indegree 0)
    JobGraphTemplate tmpl;
    auto a = tmpl.add_job({});
    auto b = tmpl.add_job({});
    auto c = tmpl.add_job({});
    tmpl.add_dependency(a, c);
    tmpl.add_dependency(b, c);

    ASSERT_TRUE(tmpl.commit());

    auto deg = tmpl.indegree();
    EXPECT_EQ(deg[a], 0u);
    EXPECT_EQ(deg[b], 0u);
    EXPECT_EQ(deg[c], 2u);
}

TEST(JobGraphTemplate, ComputesInitialReady)
{
    // A → C, B → C: roots are A and B; C is not initially ready
    JobGraphTemplate tmpl;
    auto a = tmpl.add_job({});
    auto b = tmpl.add_job({});
    auto c = tmpl.add_job({});
    tmpl.add_dependency(a, c);
    tmpl.add_dependency(b, c);

    ASSERT_TRUE(tmpl.commit());

    auto ready = tmpl.initial_ready();
    ASSERT_EQ(ready.size(), 2u);

    std::unordered_set<NodeHandle> s(ready.begin(), ready.end());
    EXPECT_TRUE(s.count(a));
    EXPECT_TRUE(s.count(b));
    EXPECT_FALSE(s.count(c));
}

TEST(JobGraphTemplate, DetectsCycles)
{
    // A → B → C → A
    JobGraphTemplate tmpl;
    auto a = tmpl.add_job({});
    auto b = tmpl.add_job({});
    auto c = tmpl.add_job({});
    tmpl.add_dependency(a, b);
    tmpl.add_dependency(b, c);
    tmpl.add_dependency(c, a);

    EXPECT_FALSE(tmpl.commit());
    EXPECT_FALSE(tmpl.committed());
}

TEST(JobGraphTemplate, SingleNodeGraphCommits)
{
    JobGraphTemplate tmpl;
    auto a = tmpl.add_job({});

    ASSERT_TRUE(tmpl.commit());
    EXPECT_EQ(tmpl.node_count(), 1u);
    EXPECT_EQ(tmpl.initial_ready().size(), 1u);
    EXPECT_EQ(tmpl.initial_ready()[0], a);
}

// ---------------------------------------------------------------------------
// FrameExecution — execution state tests
// ---------------------------------------------------------------------------

TEST(FrameExecution, ResetsFromTemplate)
{
    // A → B
    JobGraphTemplate tmpl;
    auto a = tmpl.add_job({});
    auto b = tmpl.add_job({});
    tmpl.add_dependency(a, b);
    ASSERT_TRUE(tmpl.commit());

    FrameExecution exec;
    exec.reset(tmpl);

    EXPECT_EQ(exec.status.size(), 2u);
    EXPECT_EQ(exec.status[a], JobStatus::Pending);
    EXPECT_EQ(exec.status[b], JobStatus::Pending);
    EXPECT_EQ(exec.remaining_inputs[a], 0u);
    EXPECT_EQ(exec.remaining_inputs[b], 1u);
    EXPECT_EQ(exec.remaining_jobs, 2u);
}

TEST(FrameExecution, FindsInitialReadyNodes)
{
    // A and B are roots; C depends on both
    JobGraphTemplate tmpl;
    auto a = tmpl.add_job({});
    auto b = tmpl.add_job({});
    auto c = tmpl.add_job({});
    tmpl.add_dependency(a, c);
    tmpl.add_dependency(b, c);
    ASSERT_TRUE(tmpl.commit());

    FrameExecution exec;
    exec.reset(tmpl);

    // Only A and B have zero remaining_inputs
    EXPECT_EQ(exec.remaining_inputs[a], 0u);
    EXPECT_EQ(exec.remaining_inputs[b], 0u);
    EXPECT_EQ(exec.remaining_inputs[c], 2u);
}

TEST(FrameExecution, DoesNotRebuildTopologyOnReset)
{
    JobGraphTemplate tmpl;
    tmpl.add_job({});
    ASSERT_TRUE(tmpl.commit());

    FrameExecution exec;
    exec.reset(tmpl);
    exec.status[0] = JobStatus::Done;

    exec.reset(tmpl); // second reset

    EXPECT_EQ(exec.status[0], JobStatus::Pending); // state cleared
    EXPECT_TRUE(tmpl.committed());                  // topology unchanged
}

TEST(FrameExecution, BindStoresPointer)
{
    JobGraphTemplate tmpl;
    auto a = tmpl.add_job({});
    ASSERT_TRUE(tmpl.commit());

    FrameExecution exec;
    exec.reset(tmpl);

    int data = 42;
    exec.bind(a, &data);
    EXPECT_EQ(exec.bindings[a], &data);
}

// ---------------------------------------------------------------------------
// DagScheduler — dependency ordering tests (no real threads)
// ---------------------------------------------------------------------------

TEST(DagScheduler, RunsNodesInDependencyOrder)
{
    // A → B → C: must execute A, then B, then C
    JobGraphTemplate tmpl;
    auto a = tmpl.add_job({ .run = record_fn });
    auto b = tmpl.add_job({ .run = record_fn });
    auto c = tmpl.add_job({ .run = record_fn });
    tmpl.add_dependency(a, b);
    tmpl.add_dependency(b, c);
    ASSERT_TRUE(tmpl.commit());

    FrameExecution exec;
    exec.reset(tmpl);

    std::vector<NodeHandle> order;
    bind_all(exec, tmpl, order);

    DagScheduler sched;
    sched.execute(tmpl, exec);

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], a);
    EXPECT_EQ(order[1], b);
    EXPECT_EQ(order[2], c);
}

TEST(DagScheduler, RunsIndependentNodesWithoutOrderingRequirement)
{
    // A and B have no dependency — both must run, order unspecified
    JobGraphTemplate tmpl;
    auto a = tmpl.add_job({ .run = record_fn });
    auto b = tmpl.add_job({ .run = record_fn });
    ASSERT_TRUE(tmpl.commit());

    FrameExecution exec;
    exec.reset(tmpl);

    std::vector<NodeHandle> ran;
    bind_all(exec, tmpl, ran);

    DagScheduler sched;
    sched.execute(tmpl, exec);

    ASSERT_EQ(ran.size(), 2u);
    EXPECT_TRUE(std::find(ran.begin(), ran.end(), a) != ran.end());
    EXPECT_TRUE(std::find(ran.begin(), ran.end(), b) != ran.end());
}

TEST(DagScheduler, MarksDependentsReadyAfterAllInputsComplete)
{
    // A and B are independent; C depends on both — C must run last
    JobGraphTemplate tmpl;
    auto a = tmpl.add_job({ .run = record_fn });
    auto b = tmpl.add_job({ .run = record_fn });
    auto c = tmpl.add_job({ .run = record_fn });
    tmpl.add_dependency(a, c);
    tmpl.add_dependency(b, c);
    ASSERT_TRUE(tmpl.commit());

    FrameExecution exec;
    exec.reset(tmpl);

    std::vector<NodeHandle> order;
    bind_all(exec, tmpl, order);

    DagScheduler sched;
    sched.execute(tmpl, exec);

    ASSERT_EQ(order.size(), 3u);
    // C must be last; A and B must both precede it
    EXPECT_EQ(order.back(), c);
    auto pos_c = order.size() - 1;
    EXPECT_LT(static_cast<std::size_t>(std::distance(
        order.begin(), std::find(order.begin(), order.end(), a))), pos_c);
    EXPECT_LT(static_cast<std::size_t>(std::distance(
        order.begin(), std::find(order.begin(), order.end(), b))), pos_c);
}

TEST(DagScheduler, AllNodesReachDoneStatus)
{
    JobGraphTemplate tmpl;
    auto a = tmpl.add_job({ .run = record_fn });
    auto b = tmpl.add_job({ .run = record_fn });
    auto c = tmpl.add_job({ .run = record_fn });
    tmpl.add_dependency(a, b);
    tmpl.add_dependency(a, c);
    ASSERT_TRUE(tmpl.commit());

    FrameExecution exec;
    exec.reset(tmpl);
    std::vector<NodeHandle> ran;
    bind_all(exec, tmpl, ran);

    DagScheduler sched;
    sched.execute(tmpl, exec);

    EXPECT_EQ(exec.status[a], JobStatus::Done);
    EXPECT_EQ(exec.status[b], JobStatus::Done);
    EXPECT_EQ(exec.status[c], JobStatus::Done);
    EXPECT_EQ(exec.remaining_jobs, 0u);
}

TEST(DagScheduler, NullRunFunctionIsSkippedSafely)
{
    // Nodes with run == nullptr are still "executed" (no crash) and
    // their dependents still become ready.
    JobGraphTemplate tmpl;
    auto a = tmpl.add_job({ .run = nullptr });        // no-op
    auto b = tmpl.add_job({ .run = record_fn });      // records
    tmpl.add_dependency(a, b);
    ASSERT_TRUE(tmpl.commit());

    FrameExecution exec;
    exec.reset(tmpl);
    std::vector<NodeHandle> ran;
    exec.bind(b, &ran);

    DagScheduler sched;
    sched.execute(tmpl, exec);

    EXPECT_EQ(exec.status[a], JobStatus::Done);
    ASSERT_EQ(ran.size(), 1u);
    EXPECT_EQ(ran[0], b);
}

TEST(DagScheduler, UnboundFrameUserIsNull)
{
    void* seen = reinterpret_cast<void*>(0x1); // sentinel — must become nullptr

    JobFn check_null = [](JobContext& ctx)
    {
        *static_cast<void**>(ctx.node_user) = ctx.frame_user;
    };

    JobGraphTemplate tmpl;
    tmpl.add_job({ .run = check_null, .user = &seen });
    ASSERT_TRUE(tmpl.commit());

    FrameExecution exec;
    exec.reset(tmpl); // bind() not called — bindings[0] stays nullptr

    DagScheduler sched;
    sched.execute(tmpl, exec);

    EXPECT_EQ(seen, nullptr);
}

TEST(DagScheduler, ResetAndReexecuteProducesSameResult)
{
    // Verify that resetting and re-running produces identical results
    JobGraphTemplate tmpl;
    auto a = tmpl.add_job({ .run = record_fn });
    auto b = tmpl.add_job({ .run = record_fn });
    tmpl.add_dependency(a, b);
    ASSERT_TRUE(tmpl.commit());

    DagScheduler sched;

    for (int i = 0; i < 3; ++i)
    {
        FrameExecution exec;
        exec.reset(tmpl);
        std::vector<NodeHandle> order;
        bind_all(exec, tmpl, order);
        sched.execute(tmpl, exec);

        ASSERT_EQ(order.size(), 2u);
        EXPECT_EQ(order[0], a);
        EXPECT_EQ(order[1], b);
    }
}
