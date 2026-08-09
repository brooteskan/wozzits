#include "jobs/critical_path.h"

#include "jobs/job_graph_template.h"
#include "jobs/job_profiler.h"

#include <algorithm>
#include <cstdio>

namespace wz::jobs
{
    void analyze_critical_path(const JobGraphTemplate& tmpl,
                               const FrameJobProfile&  profile,
                               CriticalPathAnalysis&   out)
    {
        out.clear();

        const uint32_t nc = tmpl.node_count();
        if (nc == 0)
            return;

        // node -> measured duration; nodes without a record stay at 0.
        std::vector<uint64_t> duration(nc, 0);
        for (const JobTimingRecord& r : profile.timings)
            if (r.node < nc)
                duration[r.node] = r.duration_ticks();

        uint64_t total = 0;
        for (uint64_t d : duration)
            total += d;
        out.total_work_ticks = total;

        // Longest-weighted-chain DP in topological order (prerequisites first).
        //   cp[n]   = duration of the heaviest dependency chain ending at n (incl. n)
        //   pred[n] = the node before n on that chain (INVALID_JOB at a root)
        // Because topo order visits every predecessor of n before n itself, cp[n]
        // is final when n is reached; we then relax n's dependents. O(V + E).
        std::vector<uint64_t>   cp(duration);
        std::vector<NodeHandle> pred(nc, INVALID_JOB);

        for (NodeHandle n : tmpl.topo_order())
        {
            const uint64_t base = cp[n];
            for (NodeHandle d : tmpl.dependents(n))
            {
                const uint64_t cand = base + duration[d];
                if (cand > cp[d])
                {
                    cp[d]   = cand;
                    pred[d] = n;
                }
            }
        }

        // The path ends at the node with the greatest cp[].
        NodeHandle end = 0;
        for (NodeHandle n = 1; n < nc; ++n)
            if (cp[n] > cp[end])
                end = n;

        out.critical_path_ticks = cp[end];

        // Trace predecessors back to the root, then flip into dependency order.
        const JobGraph& g = tmpl.graph();
        for (NodeHandle n = end; n != INVALID_JOB; n = pred[n])
        {
            const JobNode& job = wz::core::graph::node_data(g, n);
            out.path.push_back(CriticalPathEntry{
                .node           = n,
                .name           = job.name,
                .duration_ticks = duration[n],
            });
        }
        std::reverse(out.path.begin(), out.path.end());
    }

    namespace
    {
        void append_ms(std::string& s, uint64_t ticks, uint64_t ticks_per_second)
        {
            const double ms = ticks_per_second == 0
                ? 0.0
                : (static_cast<double>(ticks) * 1000.0)
                / static_cast<double>(ticks_per_second);
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.3f ms", ms);
            s += buf;
        }
    }

    std::string format_frame_report(const FrameJobProfile&      profile,
                                    const CriticalPathAnalysis& analysis,
                                    uint64_t                    ticks_per_second)
    {
        std::string s;

        char header[64];
        std::snprintf(header, sizeof(header), "frame %llu - %zu jobs\n",
                      static_cast<unsigned long long>(profile.frame_index),
                      profile.timings.size());
        s += header;

        // Per-job durations, in execution (completion) order.
        for (const JobTimingRecord& r : profile.timings)
        {
            s += "  ";
            s += (r.name ? r.name : "<unnamed>");
            s += ": ";
            append_ms(s, r.duration_ticks(), ticks_per_second);
            s += '\n';
        }

        // Critical path.
        s += "critical path (";
        append_ms(s, analysis.critical_path_ticks, ticks_per_second);
        char pc[32];
        std::snprintf(pc, sizeof(pc), ", %zu jobs):\n", analysis.path.size());
        s += pc;
        s += "  ";
        for (size_t i = 0; i < analysis.path.size(); ++i)
        {
            if (i)
                s += " -> ";
            s += (analysis.path[i].name ? analysis.path[i].name : "<unnamed>");
        }
        s += '\n';

        // Summary line - the gate signal.
        s += "total work ";
        append_ms(s, analysis.total_work_ticks, ticks_per_second);
        s += " | critical path ";
        append_ms(s, analysis.critical_path_ticks, ticks_per_second);
        char par[32];
        std::snprintf(par, sizeof(par), " | parallelism %.2fx\n", analysis.parallelism());
        s += par;

        return s;
    }

    std::string format_frame_report_line(const FrameJobProfile&      profile,
                                         const CriticalPathAnalysis& analysis,
                                         uint64_t                    ticks_per_second)
    {
        const double to_ms = ticks_per_second == 0
            ? 0.0
            : 1000.0 / static_cast<double>(ticks_per_second);

        std::string s;
        char buf[64];

        std::snprintf(buf, sizeof(buf), "frame %llu phases(ms):",
                      static_cast<unsigned long long>(profile.frame_index));
        s += buf;

        for (const JobTimingRecord& r : profile.timings)
        {
            s += ' ';
            s += (r.name ? r.name : "<unnamed>");
            std::snprintf(buf, sizeof(buf), "=%.2f",
                          static_cast<double>(r.duration_ticks()) * to_ms);
            s += buf;
        }

        std::snprintf(buf, sizeof(buf), " | crit=%.2f total=%.2f par=%.2fx",
                      static_cast<double>(analysis.critical_path_ticks) * to_ms,
                      static_cast<double>(analysis.total_work_ticks) * to_ms,
                      analysis.parallelism());
        s += buf;

        return s;
    }
}
