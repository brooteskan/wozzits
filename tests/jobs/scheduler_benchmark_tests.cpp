// tests/jobs/scheduler_benchmark_tests.cpp
//
// Synthetic scheduler-overhead benchmarks (design doc 5.1.3). They bound the
// DagScheduler's per-job / per-edge cost and the FrameExecution::reset /
// JobGraphTemplate::commit cost, so job-splitting decisions can be made with
// evidence rather than guesswork. No-op jobs isolate the scheduler from job
// bodies.
//
// This is a measurement tool, not a correctness gate: it prints a table and
// SUCCEED()s (only light sanity checks). Numbers are machine-specific. Runs by
// default like tests/render/pipeline_benchmark_test_1.cpp; sizes are kept modest.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include <jobs/job_graph_template.h>
#include <jobs/frame_execution.h>
#include <jobs/dag_scheduler.h>

using namespace wz::jobs;

namespace
{
    JobFn noop = [](JobContext&) {};

    // ── Graph shapes (5.1.3) ────────────────────────────────────────────────

    // A -> B -> C -> ... (n nodes, n-1 edges).
    void populate_chain(JobGraphTemplate& t, uint32_t n)
    {
        NodeHandle prev = t.add_job({ .name = "j", .run = noop });
        for (uint32_t i = 1; i < n; ++i)
        {
            NodeHandle cur = t.add_job({ .name = "j", .run = noop });
            t.add_dependency(prev, cur);
            prev = cur;
        }
    }

    // n independent jobs, no edges.
    void populate_independent(JobGraphTemplate& t, uint32_t n)
    {
        for (uint32_t i = 0; i < n; ++i)
            t.add_job({ .name = "j", .run = noop });
    }

    // root -> k middles -> join (k+2 nodes, 2k edges).
    void populate_fanout(JobGraphTemplate& t, uint32_t k)
    {
        NodeHandle root = t.add_job({ .name = "root", .run = noop });
        NodeHandle join = t.add_job({ .name = "join", .run = noop });
        for (uint32_t i = 0; i < k; ++i)
        {
            NodeHandle mid = t.add_job({ .name = "mid", .run = noop });
            t.add_dependency(root, mid);
            t.add_dependency(mid, join);
        }
    }

    // Render-like chain of 4 + s independent side jobs (4+s nodes, 3 edges).
    void populate_mixed(JobGraphTemplate& t, uint32_t s)
    {
        NodeHandle a = t.add_job({ .name = "build_view", .run = noop });
        NodeHandle b = t.add_job({ .name = "compile_scene", .run = noop });
        NodeHandle c = t.add_job({ .name = "build_render_ir", .run = noop });
        NodeHandle d = t.add_job({ .name = "build_render_frame", .run = noop });
        t.add_dependency(a, b);
        t.add_dependency(b, c);
        t.add_dependency(c, d);
        for (uint32_t i = 0; i < s; ++i)
            t.add_job({ .name = "side", .run = noop });
    }

    // ── Timing ──────────────────────────────────────────────────────────────

    template <typename Fn>
    double time_us(uint32_t iters, Fn&& fn)
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        for (uint32_t i = 0; i < iters; ++i)
            fn();
        auto t1 = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::micro>(t1 - t0).count() / iters;
    }

    // execute() cost isolated from reset(): time (reset+execute), subtract reset.
    // execute() mutates FrameExecution, so a reset is required before each run.
    struct ExecTiming { double exec_us; double reset_us; };

    ExecTiming measure_exec(const JobGraphTemplate& t, uint32_t iters)
    {
        FrameExecution exec;
        exec.reset(t);
        DagScheduler sched;
        sched.execute(t, exec);                                   // warm
        EXPECT_EQ(exec.remaining_jobs, 0u);                       // sanity: graph drained

        const double reset_us = time_us(iters, [&] { exec.reset(t); });
        const double both_us  = time_us(iters, [&] { exec.reset(t); sched.execute(t, exec); });
        return ExecTiming{ both_us - reset_us, reset_us };
    }

    template <typename Populate>
    double measure_commit_us(Populate&& populate, uint32_t iters)
    {
        // commit() is one-shot per template, so pre-populate `iters` templates
        // (untimed) and time only the commit() calls.
        std::vector<JobGraphTemplate> temps(iters);
        for (auto& t : temps)
            populate(t);

        auto t0 = std::chrono::high_resolution_clock::now();
        for (auto& t : temps)
        {
            const bool ok = t.commit();
            EXPECT_TRUE(ok);
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::micro>(t1 - t0).count() / iters;
    }
}

TEST(SchedulerBenchmark, Overhead)
{
    constexpr uint32_t M_EXEC = 100;

    std::printf("\nScheduler benchmark (no-op jobs, DagScheduler::execute; machine-specific)\n");
    std::printf("  %-24s %8s %8s %10s %9s\n", "shape", "nodes", "edges", "us/call", "ns/node");
    std::printf("  %s\n", std::string(63, '-').c_str());

    double indep10k_exec = 0.0, chain10k_exec = 0.0;

    auto run_shape = [&](const char* label, const JobGraphTemplate& t,
                         uint32_t nodes, uint32_t edges) {
        const ExecTiming e = measure_exec(t, M_EXEC);
        const double ns_per_node = e.exec_us * 1000.0 / nodes;
        std::printf("  %-24s %8u %8u %10.2f %9.2f\n",
                    label, nodes, edges, e.exec_us, ns_per_node);
        return e.exec_us;
    };

    { JobGraphTemplate t; populate_independent(t, 100);   ASSERT_TRUE(t.commit()); run_shape("independent x100",   t, 100,   0); }
    { JobGraphTemplate t; populate_independent(t, 10000); ASSERT_TRUE(t.commit()); indep10k_exec = run_shape("independent x10000", t, 10000, 0); }
    { JobGraphTemplate t; populate_chain(t, 100);         ASSERT_TRUE(t.commit()); run_shape("chain x100",         t, 100,   99); }
    { JobGraphTemplate t; populate_chain(t, 10000);       ASSERT_TRUE(t.commit()); chain10k_exec = run_shape("chain x10000",       t, 10000, 9999); }
    { JobGraphTemplate t; populate_fanout(t, 1000);       ASSERT_TRUE(t.commit()); run_shape("fanout(1000)",       t, 1002,  2000); }
    { JobGraphTemplate t; populate_mixed(t, 1000);        ASSERT_TRUE(t.commit()); run_shape("mixed(chain4+1000)", t, 1004,  3); }

    // ── Derived per-job / per-edge overhead ──────────────────────────────────
    // Both 10k shapes dispatch the same node count, so the per-node dispatch cost
    // cancels in the difference; what remains is attributable to the 9999 edges
    // (dependency decrements). Approximate — edge work and ready-set growth are
    // not perfectly separable.
    const double per_job_ns  = indep10k_exec * 1000.0 / 10000.0;
    const double per_edge_ns = (chain10k_exec - indep10k_exec) * 1000.0 / 9999.0;
    std::printf("\n  per-job overhead  ~ %6.2f ns   (independent x10000)\n", per_job_ns);
    std::printf("  per-edge overhead ~ %6.2f ns   (chain - independent, approx)\n", per_edge_ns);

    // ── reset + commit ───────────────────────────────────────────────────────
    std::printf("\n  FrameExecution::reset + JobGraphTemplate::commit\n");
    std::printf("  %-24s %8s %8s %10s %10s\n", "shape", "nodes", "edges", "reset us", "commit us");
    std::printf("  %s\n", std::string(65, '-').c_str());

    {
        JobGraphTemplate t; populate_independent(t, 10000); ASSERT_TRUE(t.commit());
        const ExecTiming e = measure_exec(t, M_EXEC);
        const double commit_us = measure_commit_us([](JobGraphTemplate& g){ populate_independent(g, 10000); }, 20);
        std::printf("  %-24s %8u %8u %10.3f %10.2f\n", "independent x10000", 10000u, 0u, e.reset_us, commit_us);
    }
    {
        JobGraphTemplate t; populate_chain(t, 10000); ASSERT_TRUE(t.commit());
        const ExecTiming e = measure_exec(t, M_EXEC);
        const double commit_us = measure_commit_us([](JobGraphTemplate& g){ populate_chain(g, 10000); }, 20);
        std::printf("  %-24s %8u %8u %10.3f %10.2f\n", "chain x10000", 10000u, 9999u, e.reset_us, commit_us);
    }

    std::printf("\n");
    SUCCEED();
}
