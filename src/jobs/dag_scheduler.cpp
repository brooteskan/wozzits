#include "jobs/dag_scheduler.h"
#include "jobs/job_graph_template.h"
#include "jobs/frame_execution.h"

#include <cassert>

namespace wz::jobs
{
    void DagScheduler::execute(const JobGraphTemplate& tmpl, FrameExecution& exec)
    {
        assert(tmpl.committed());

        ready_.clear();
        for (NodeHandle n : tmpl.initial_ready())
        {
            exec.status[n] = JobStatus::Ready;
            ready_.push_back(n);
        }

        while (!ready_.empty())
        {
            NodeHandle n = ready_.back();
            ready_.pop_back();
            run_node(tmpl, exec, n);
        }
    }

    void DagScheduler::run_node(const JobGraphTemplate& tmpl, FrameExecution& exec, NodeHandle n)
    {
        exec.status[n] = JobStatus::Running;

        const JobNode& job = wz::core::graph::node_data(tmpl.graph(), n);
        if (job.run)
        {
            JobContext ctx;
            ctx.node       = n;
            ctx.node_user  = job.user;
            ctx.frame_user = exec.bindings[n];
            job.run(ctx);
        }

        complete_node(tmpl, exec, n);
    }

    void DagScheduler::complete_node(const JobGraphTemplate& tmpl, FrameExecution& exec, NodeHandle n)
    {
        exec.status[n] = JobStatus::Done;
        --exec.remaining_jobs;

        for (NodeHandle dep : tmpl.dependents(n))
        {
            if (--exec.remaining_inputs[dep] == 0)
            {
                exec.status[dep] = JobStatus::Ready;
                ready_.push_back(dep);
            }
        }
    }

    // ── FrameExecution implementation ────────────────────────────────────────────
    // Defined here to avoid a circular include: frame_execution.h forward-declares
    // JobGraphTemplate, so the reset() body must see the full definition.

    void FrameExecution::reset(const JobGraphTemplate& tmpl)
    {
        assert(tmpl.committed());
        const uint32_t nc = tmpl.node_count();

        status.assign(nc, JobStatus::Pending);

        remaining_inputs.resize(nc);
        std::memcpy(remaining_inputs.data(), tmpl.indegree().data(), nc * sizeof(uint32_t));

        bindings.assign(nc, nullptr);

        remaining_jobs = nc;
    }

    void FrameExecution::bind(NodeHandle n, void* frame_data)
    {
        assert(n < bindings.size() && "bind() called with handle out of range — was reset() called first?");
        bindings[n] = frame_data;
    }

} // namespace wz::jobs
