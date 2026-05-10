#pragma once

// DagScheduler — single-threaded dependency scheduler (Phase 1).
//
// Responsibility:
//   - Determine which nodes are ready (remaining_inputs reaches zero).
//   - Dispatch ready nodes by invoking JobNode::run with a JobContext.
//   - Advance the dependency graph as nodes complete.
//
// The scheduler does NOT own thread pools or decide where work runs.
// Worker pool dispatch is Phase 2; this version executes on the calling thread.
//
// The scheduler DOES NOT mutate JobGraphTemplate topology.
// Only FrameExecution (transient state) is modified during a run.

#include <vector>
#include "job_types.h"

namespace wz::jobs
{
    class JobGraphTemplate;
    struct FrameExecution;

    class DagScheduler
    {
    public:
        DagScheduler()  = default;
        ~DagScheduler() = default;

        DagScheduler(const DagScheduler&)            = delete;
        DagScheduler& operator=(const DagScheduler&) = delete;

        // Execute all jobs in dependency order on the calling thread.
        // exec must have been reset from tmpl before calling.
        void execute(const JobGraphTemplate& tmpl, FrameExecution& exec);

    private:
        void run_node    (const JobGraphTemplate& tmpl, FrameExecution& exec, NodeHandle n);
        void complete_node(const JobGraphTemplate& tmpl, FrameExecution& exec, NodeHandle n);

        std::vector<NodeHandle> ready_; // reused across execute() calls — avoids per-run alloc
    };

} // namespace wz::jobs
