#pragma once

#include <cstdint>
#include <vector>
#include <jobs/job_types.h>

namespace wz::jobs
{
    struct JobTimingRecord
    {
        NodeHandle  node        = INVALID_JOB;
        const char* name        = nullptr;
        uint64_t    start_ticks = 0;
        uint64_t    end_ticks   = 0;

        uint64_t duration_ticks() const { return end_ticks - start_ticks; }
    };

    struct FrameJobProfile
    {
        uint64_t                     frame_index = 0;
        std::vector<JobTimingRecord> timings;

        void reset(uint64_t frame)
        {
            frame_index = frame;
            timings.clear();
        }

        void record(
            NodeHandle  node,
            const char* name,
            uint64_t    start_ticks,
            uint64_t    end_ticks)
        {
            timings.push_back(JobTimingRecord{
                .node        = node,
                .name        = name,
                .start_ticks = start_ticks,
                .end_ticks   = end_ticks,
            });
        }
    };

} // namespace wz::jobs
