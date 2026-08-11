#pragma once

// frame_schedule.h — the per-frame phase DAG, driven live through DagScheduler.
//
// This is the seam that puts the running frame on the job scheduler (#305). Today
// the host loop calls the frame's phases inline and in a fixed order; FrameSchedule
// expresses that same order as a committed JobGraphTemplate and runs it through
// DagScheduler, so the frame is a real (if currently linear) job graph — the thing
// per-job timing, critical-path analysis, and the later lane build-out all hang off.
//
// Scope of this first increment:
//   - Strict linear chain in the EXISTING order — behaviour-preserving. The phases
//     are inherently serial today (render reads what simulate produced, between the
//     GPU begin/end framing), so the critical path is the whole frame and
//     parallelism is 1.0. Relaxing edges to expose slack is a later step.
//   - All phases on ExecutionLane::MainThread; lane assignment (GPUOwner for the
//     GPU framing phases) waits on the lane build-out.
//   - Phases are supplied as callbacks, so the schedule is decoupled from the host
//     loop and unit-testable with mock phases.
//
// Not a replacement for the app's frame_profile_ CSV (#252) — this is the jobs-level
// per-phase profile feeding analyze_critical_path.

#include <cstdint>
#include <functional>

#include <jobs/job_graph_template.h>
#include <jobs/frame_execution.h>
#include <jobs/dag_scheduler.h>
#include <jobs/job_profiler.h>
#include <jobs/critical_path.h>

namespace wz::engine
{
    // The frame's phases, in dependency order. Node handle == phase index, so the
    // enum order IS the graph order (see FrameSchedule's constructor).
    enum class FramePhase : uint32_t
    {
        Simulate,        // simulation_tick / paused_frame_tick (CPU sim)
        BeginFrame,      // gpu::begin_frame
        Clear,           // gpu::clear
        RenderScene,     // app.render_scene   (records draws)
        RenderOverlay,   // app.render_puppet_showcase
        EndFrame,        // gpu::end_frame
        Present,         // gpu::present
        Count,
    };

    inline constexpr uint32_t kFramePhaseCount = static_cast<uint32_t>(FramePhase::Count);

    const char* to_string(FramePhase phase);

    // One frame's phase operations. Each returns true on success; a false return
    // stops the frame at that phase (mirroring the host loop's error-break). An
    // unset (empty) callback is treated as a no-op that succeeds.
    struct FramePhaseOps
    {
        std::function<bool()> simulate;
        std::function<bool()> begin_frame;
        std::function<bool()> clear;
        std::function<bool()> render_scene;
        std::function<bool()> render_overlay;
        std::function<bool()> end_frame;
        std::function<bool()> present;

        // NOT a phase -- the unwind. Invoked by run() when the frame stopped at a
        // phase BETWEEN begin_frame and end_frame, i.e. exactly when begin_frame
        // succeeded and end_frame will never run.
        //
        // It exists because "stop at the first failing phase" silently breaks the
        // GPU's begin/end bracket: the backend's command list is opened by
        // begin_frame and closed only by end_frame, and a list that is never
        // closed can never be reset -- so every SUBSEQUENT frame fails to begin,
        // with the device still reporting itself healthy. Skipping end_frame is
        // right (the recording is incomplete and must not be submitted); skipping
        // the close is not. wz::gpu::abort_frame is the intended implementation.
        //
        // Leaving this unset is fine for a schedule with no GPU framing (the
        // mock-phase tests), and is the pre-existing behaviour otherwise.
        std::function<void()> abort_frame;
    };

    class FrameSchedule
    {
    public:
        FrameSchedule();   // builds + commits the phase DAG once

        FrameSchedule(const FrameSchedule&)            = delete;
        FrameSchedule& operator=(const FrameSchedule&) = delete;

        struct Result
        {
            bool       ok           = true;
            FramePhase failed_phase = FramePhase::Count;  // valid only when !ok
        };

        // Run one frame: execute the phases in dependency order on the calling
        // thread, stopping at the first phase that returns false. When profiling is
        // enabled, records a FrameJobProfile and refreshes the critical-path
        // analysis (both queryable below).
        Result run(const FramePhaseOps& ops);

        // Per-phase profiling — off by default. Feeds last_profile()/last_analysis().
        void set_profiling(bool on) { profiling_ = on; }
        bool profiling() const { return profiling_; }

        // Results of the most recent profiled run (empty/cleared if profiling off).
        const jobs::FrameJobProfile&      last_profile()  const { return profile_; }
        const jobs::CriticalPathAnalysis& last_analysis() const { return analysis_; }

    private:
        jobs::JobGraphTemplate     graph_;
        jobs::FrameExecution       exec_;
        jobs::DagScheduler         sched_;
        jobs::FrameJobProfile      profile_;
        jobs::CriticalPathAnalysis analysis_;
        bool                       profiling_   = false;
        uint64_t                   frame_index_ = 0;
    };
}
