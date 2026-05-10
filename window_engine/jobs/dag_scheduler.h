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
//
// Optional profiling: attach a FrameJobProfile via set_profile() before execute().
// The scheduler records per-job timing into the profile if one is set.
// The scheduler does not own the profile.

#include <vector>
#include "job_types.h"

namespace wz::jobs
{
    class JobGraphTemplate;
    struct FrameExecution;
    struct FrameJobProfile;

    class DagScheduler
    {
    public:
        DagScheduler()  = default;
        ~DagScheduler() = default;

        DagScheduler(const DagScheduler&)            = delete;
        DagScheduler& operator=(const DagScheduler&) = delete;

        // Attach or detach a per-frame profiling sink.
        // The caller owns the profile; it must remain valid until execute() returns.
        // Pass nullptr to disable profiling (the default).
        void set_profile(FrameJobProfile* profile) { profile_ = profile; }

        // Execute all jobs in dependency order on the calling thread.
        // exec must have been reset from tmpl before calling.
        void execute(const JobGraphTemplate& tmpl, FrameExecution& exec);

    private:
        void run_node    (const JobGraphTemplate& tmpl, FrameExecution& exec, NodeHandle n);
        void complete_node(const JobGraphTemplate& tmpl, FrameExecution& exec, NodeHandle n);

        std::vector<NodeHandle> ready_;             // reused across execute() calls
        FrameJobProfile*        profile_ = nullptr; // non-owning; null == profiling disabled
    };

} // namespace wz::jobs
