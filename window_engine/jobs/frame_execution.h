#pragma once

// FrameExecution — lightweight per-run scheduling state.
//
// Does NOT own job payload memory.
// bindings[] holds non-owning pointers to caller-supplied data valid for the
// duration of one execution. The caller is responsible for lifetime.
//
// Reset is cheap: status and remaining_inputs are filled from the precomputed
// template data (a memcpy for remaining_inputs, a fill for status), bindings
// are zeroed. No topology is rebuilt.

#include <cstdint>
#include <cstring>
#include <vector>

#include "job_types.h"

namespace wz::jobs
{
    class JobGraphTemplate; // forward — frame_execution.h must not include the template header

    struct FrameExecution
    {
        std::vector<JobStatus> status;
        std::vector<uint32_t>  remaining_inputs;
        std::vector<void*>     bindings;       // per-node frame data, caller-owned
        uint32_t               remaining_jobs = 0;

        // Resize and reset all fields from the committed template.
        void reset(const JobGraphTemplate& tmpl);

        // Bind per-run data to a node. Called after reset(), before execute().
        // frame_data must remain valid until execution completes.
        void bind(NodeHandle n, void* frame_data);
    };

} // namespace wz::jobs
