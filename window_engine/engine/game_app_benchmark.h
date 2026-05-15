#pragma once
// engine/game_app_benchmark.h

#include <cstdint>

namespace wz::engine
{
    struct FrameContext;
}

namespace wz::app
{
    struct GameApp;

    static constexpr uint32_t kGameAppBenchmarkMaxJobs = 32;

    struct GameAppJobTimingSnapshot
    {
        const char* name = "";
        double ms = 0.0;
    };

    struct GameAppBenchmarkSnapshot
    {
        uint64_t frame_index = 0;

        double dt_seconds = 0.0;
        double fps = 0.0;

        uint64_t scene_nodes = 0;
        uint64_t debug_renderables = 0;
        uint64_t dirty_nodes = 0;

        uint64_t opaque_commands = 0;
        uint64_t splat_commands = 0;
        uint64_t transparent_commands = 0;
        uint64_t particle_commands = 0;
        uint64_t total_commands = 0;

        uint64_t bytes_owned = 0;
        uint64_t bytes_allocated_this_frame = 0;
        uint32_t reallocations_this_frame = 0;

        const char* render_prep_path = "Unknown";
        const char* debug_scene_mode = "";
        const char* debug_animation = "";
        bool debug_culling_disabled = false;

        bool debug_object_ready = false;
        bool compiled_scene_valid = false;

        uint32_t job_count = 0;
        GameAppJobTimingSnapshot jobs[kGameAppBenchmarkMaxJobs]{};

        double total_job_ms = 0.0;
        double render_prep_job_ms = 0.0;

        const char* slowest_job_name = "";
        double slowest_job_ms = 0.0;
    };

    GameAppBenchmarkSnapshot benchmark_snapshot(
        const GameApp& app,
        const wz::engine::FrameContext& fctx);
}
