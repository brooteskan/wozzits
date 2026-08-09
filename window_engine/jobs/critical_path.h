#pragma once

// critical_path.h — longest-weighted-path analysis over a frame's job timings.
//
// Per-job timing (JobProfiler) answers design-doc questions 1-2 ("which jobs run",
// "how long does each take"). This answers question 4, "which jobs are on the
// critical path" — the longest duration-weighted dependency chain through the
// committed graph. That chain is the frame's wall-clock lower bound under
// unlimited parallelism, and the ratio total_work / critical_path is the gate
// signal for off-frame-loop dispatch:
//   ~1.0   the frame is essentially serial — moving work off-thread buys little.
//   >> 1.0 there is real slack a lane runtime could exploit.
//
// See window_engine/jobs/MemoryManagementDesignDoc.md 5.1 / 5.3: instrumentation
// must show critical-path pressure before async dispatch is introduced.

#include <cstdint>
#include <string>
#include <vector>

#include "job_types.h"

namespace wz::jobs
{
    class JobGraphTemplate;
    struct FrameJobProfile;

    // One node on the critical path, listed root-first in dependency order.
    struct CriticalPathEntry
    {
        NodeHandle  node           = INVALID_JOB;
        const char* name           = nullptr;
        uint64_t    duration_ticks = 0;
    };

    // Critical-path analysis of one frame's timings.
    struct CriticalPathAnalysis
    {
        std::vector<CriticalPathEntry> path;                 // root -> ... -> end
        uint64_t total_work_ticks    = 0;                    // sum of every node's duration
        uint64_t critical_path_ticks = 0;                    // sum of durations along path

        // total_work / critical_path — the average width of the graph. Always
        // >= 1.0; exactly 1.0 for a purely serial chain. Returns 1.0 when no
        // measurable work was recorded (critical_path_ticks == 0).
        double parallelism() const
        {
            return critical_path_ticks == 0
                ? 1.0
                : static_cast<double>(total_work_ticks)
                / static_cast<double>(critical_path_ticks);
        }

        void clear()
        {
            path.clear();
            total_work_ticks    = 0;
            critical_path_ticks = 0;
        }
    };

    // Compute the critical path of `tmpl` weighted by the per-node durations in
    // `profile`, writing the result into `out` (reusing its path vector capacity).
    //
    // Nodes with no timing record contribute zero duration. `tmpl` must be
    // committed and every profile record's node handle must index into `tmpl`.
    // O(nodes + edges).
    void analyze_critical_path(const JobGraphTemplate& tmpl,
                               const FrameJobProfile&  profile,
                               CriticalPathAnalysis&   out);

    // Render a one-frame report: each job's duration (in execution order), the
    // critical path, and the total-work / critical-path / parallelism summary.
    // `ticks_per_second` converts ticks to milliseconds — pass
    // wz::time::TimeSource::ticks_per_second() live; an explicit argument keeps the
    // formatter clock-independent and testable.
    std::string format_frame_report(const FrameJobProfile&      profile,
                                    const CriticalPathAnalysis& analysis,
                                    uint64_t                    ticks_per_second);
}
