// tests/jobs/critical_path_tests.cpp
//
// Critical-path analysis is verified against hand-built profiles with synthetic
// durations, so the assertions are independent of the real high-resolution clock
// (a no-op job can measure 0 ticks). DagScheduler -> FrameJobProfile recording is
// covered separately in profiler_tests.cpp.

#include <gtest/gtest.h>

#include <string>

#include <jobs/job_graph_template.h>
#include <jobs/job_profiler.h>
#include <jobs/critical_path.h>

using namespace wz::jobs;

namespace
{
    NodeHandle add(JobGraphTemplate& t, const char* name)
    {
        return t.add_job({ .name = name });
    }

    // Record node `n` (named `name`) as having taken `dur` ticks.
    void record(FrameJobProfile& p, NodeHandle n, const char* name, uint64_t dur)
    {
        p.record(n, name, 0, dur);
    }

    // Names along the critical path, in order.
    std::vector<std::string> path_names(const CriticalPathAnalysis& a)
    {
        std::vector<std::string> out;
        for (const CriticalPathEntry& e : a.path)
            out.emplace_back(e.name ? e.name : "");
        return out;
    }
}

// ---------------------------------------------------------------------------
// Degenerate shapes
// ---------------------------------------------------------------------------

TEST(CriticalPath, EmptyGraphIsEmptyAnalysis)
{
    JobGraphTemplate t;
    ASSERT_TRUE(t.commit());

    FrameJobProfile profile;
    CriticalPathAnalysis a;
    analyze_critical_path(t, profile, a);

    EXPECT_TRUE(a.path.empty());
    EXPECT_EQ(a.total_work_ticks, 0u);
    EXPECT_EQ(a.critical_path_ticks, 0u);
    EXPECT_DOUBLE_EQ(a.parallelism(), 1.0);
}

TEST(CriticalPath, SingleNode)
{
    JobGraphTemplate t;
    auto a0 = add(t, "A");
    ASSERT_TRUE(t.commit());

    FrameJobProfile profile;
    record(profile, a0, "A", 42);

    CriticalPathAnalysis a;
    analyze_critical_path(t, profile, a);

    EXPECT_EQ(a.total_work_ticks, 42u);
    EXPECT_EQ(a.critical_path_ticks, 42u);
    EXPECT_EQ(path_names(a), (std::vector<std::string>{ "A" }));
    EXPECT_DOUBLE_EQ(a.parallelism(), 1.0);
}

// ---------------------------------------------------------------------------
// Serial chain — the "no slack" case the gate is looking to rule out
// ---------------------------------------------------------------------------

TEST(CriticalPath, SerialChainIsFullyOnPathParallelismOne)
{
    JobGraphTemplate t;
    auto a0 = add(t, "A");
    auto b0 = add(t, "B");
    auto c0 = add(t, "C");
    t.add_dependency(a0, b0);
    t.add_dependency(b0, c0);
    ASSERT_TRUE(t.commit());

    FrameJobProfile profile;
    record(profile, a0, "A", 10);
    record(profile, b0, "B", 20);
    record(profile, c0, "C", 30);

    CriticalPathAnalysis a;
    analyze_critical_path(t, profile, a);

    EXPECT_EQ(a.total_work_ticks, 60u);
    EXPECT_EQ(a.critical_path_ticks, 60u);
    EXPECT_EQ(path_names(a), (std::vector<std::string>{ "A", "B", "C" }));
    EXPECT_DOUBLE_EQ(a.parallelism(), 1.0);  // total == critical: no exploitable slack
}

// ---------------------------------------------------------------------------
// Diamond — the heavier branch wins
// ---------------------------------------------------------------------------

TEST(CriticalPath, DiamondPicksHeavierBranch)
{
    // A -> B -> D and A -> C -> D. B(100) dominates C(5).
    JobGraphTemplate t;
    auto a0 = add(t, "A");
    auto b0 = add(t, "B");
    auto c0 = add(t, "C");
    auto d0 = add(t, "D");
    t.add_dependency(a0, b0);
    t.add_dependency(a0, c0);
    t.add_dependency(b0, d0);
    t.add_dependency(c0, d0);
    ASSERT_TRUE(t.commit());

    FrameJobProfile profile;
    record(profile, a0, "A", 10);
    record(profile, b0, "B", 100);
    record(profile, c0, "C", 5);
    record(profile, d0, "D", 20);

    CriticalPathAnalysis a;
    analyze_critical_path(t, profile, a);

    EXPECT_EQ(a.total_work_ticks, 135u);        // 10 + 100 + 5 + 20
    EXPECT_EQ(a.critical_path_ticks, 130u);     // A -> B -> D
    EXPECT_EQ(path_names(a), (std::vector<std::string>{ "A", "B", "D" }));
    EXPECT_NEAR(a.parallelism(), 135.0 / 130.0, 1e-9);
}

// ---------------------------------------------------------------------------
// Wide fan-out — the case with real slack to exploit
// ---------------------------------------------------------------------------

TEST(CriticalPath, FanOutExposesParallelism)
{
    // R -> {P0,P1,P2,P3} -> J. P0(100) is the tall pole.
    JobGraphTemplate t;
    auto r0 = add(t, "R");
    auto p0 = add(t, "P0");
    auto p1 = add(t, "P1");
    auto p2 = add(t, "P2");
    auto p3 = add(t, "P3");
    auto j0 = add(t, "J");
    for (auto p : { p0, p1, p2, p3 })
    {
        t.add_dependency(r0, p);
        t.add_dependency(p, j0);
    }
    ASSERT_TRUE(t.commit());

    FrameJobProfile profile;
    record(profile, r0, "R", 1);
    record(profile, p0, "P0", 100);
    record(profile, p1, "P1", 40);
    record(profile, p2, "P2", 40);
    record(profile, p3, "P3", 40);
    record(profile, j0, "J", 1);

    CriticalPathAnalysis a;
    analyze_critical_path(t, profile, a);

    EXPECT_EQ(a.total_work_ticks, 222u);        // 1 + 100+40+40+40 + 1
    EXPECT_EQ(a.critical_path_ticks, 102u);     // R -> P0 -> J
    EXPECT_EQ(path_names(a), (std::vector<std::string>{ "R", "P0", "J" }));
    EXPECT_NEAR(a.parallelism(), 222.0 / 102.0, 1e-9);
    EXPECT_GT(a.parallelism(), 2.0);            // meaningful slack -> gate would say "worth it"
}

// ---------------------------------------------------------------------------
// Missing records contribute zero, but stay on the path
// ---------------------------------------------------------------------------

TEST(CriticalPath, NodeWithoutTimingCountsAsZero)
{
    JobGraphTemplate t;
    auto a0 = add(t, "A");
    auto b0 = add(t, "B");
    auto c0 = add(t, "C");
    t.add_dependency(a0, b0);
    t.add_dependency(b0, c0);
    ASSERT_TRUE(t.commit());

    FrameJobProfile profile;
    record(profile, a0, "A", 10);
    // B has no record -> zero duration.
    record(profile, c0, "C", 30);

    CriticalPathAnalysis a;
    analyze_critical_path(t, profile, a);

    EXPECT_EQ(a.total_work_ticks, 40u);
    EXPECT_EQ(a.critical_path_ticks, 40u);
    EXPECT_EQ(path_names(a), (std::vector<std::string>{ "A", "B", "C" }));

    ASSERT_EQ(a.path.size(), 3u);
    EXPECT_EQ(a.path[1].duration_ticks, 0u);    // B recorded nothing
}

// ---------------------------------------------------------------------------
// Two disconnected components — the globally longest chain wins
// ---------------------------------------------------------------------------

TEST(CriticalPath, LongestChainAcrossDisconnectedComponents)
{
    // Component 1: X -> Y (total 50). Component 2: Z (200, standalone).
    JobGraphTemplate t;
    auto x0 = add(t, "X");
    auto y0 = add(t, "Y");
    auto z0 = add(t, "Z");
    t.add_dependency(x0, y0);
    ASSERT_TRUE(t.commit());

    FrameJobProfile profile;
    record(profile, x0, "X", 20);
    record(profile, y0, "Y", 30);
    record(profile, z0, "Z", 200);

    CriticalPathAnalysis a;
    analyze_critical_path(t, profile, a);

    EXPECT_EQ(a.total_work_ticks, 250u);
    EXPECT_EQ(a.critical_path_ticks, 200u);     // the standalone Z dominates X->Y (50)
    EXPECT_EQ(path_names(a), (std::vector<std::string>{ "Z" }));
}

// ---------------------------------------------------------------------------
// Report formatting
// ---------------------------------------------------------------------------

TEST(CriticalPathReport, FormatsDurationsPathAndParallelism)
{
    JobGraphTemplate t;
    auto a0 = add(t, "A");
    auto b0 = add(t, "B");
    t.add_dependency(a0, b0);
    ASSERT_TRUE(t.commit());

    FrameJobProfile profile;
    profile.reset(7);
    record(profile, a0, "A", 1000);   // 1000 ticks
    record(profile, b0, "B", 3000);   // 3000 ticks

    CriticalPathAnalysis a;
    analyze_critical_path(t, profile, a);

    // 1,000,000 ticks/sec -> 1 tick = 0.001 ms, so 1000 ticks = 1.000 ms.
    const std::string report = format_frame_report(profile, a, 1'000'000);

    EXPECT_NE(report.find("frame 7 - 2 jobs"), std::string::npos);
    EXPECT_NE(report.find("A: 1.000 ms"), std::string::npos);
    EXPECT_NE(report.find("B: 3.000 ms"), std::string::npos);
    EXPECT_NE(report.find("A -> B"), std::string::npos);
    EXPECT_NE(report.find("parallelism 1.00x"), std::string::npos);
}
