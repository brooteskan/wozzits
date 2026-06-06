#pragma once
// stats/scene_render_storage.h

#include <cstdint>

namespace wz::scene_render
{
    struct AllocationStats
    {
        uint64_t bytes_owned = 0;
        uint64_t bytes_allocated_this_build = 0;
        uint32_t reallocations_this_build = 0;

        void reset_build_counters()
        {
            bytes_allocated_this_build = 0;
            reallocations_this_build = 0;
        }

        void record_owned(uint64_t bytes)
        {
            bytes_owned = bytes;
        }

        void record_reallocation(uint64_t bytes)
        {
            bytes_allocated_this_build += bytes;
            reallocations_this_build += 1;
            bytes_owned = bytes;
        }
    };
}