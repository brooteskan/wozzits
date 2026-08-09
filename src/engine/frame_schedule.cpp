#include "engine/frame_schedule.h"

#include <cassert>

namespace wz::engine
{
    const char* to_string(FramePhase phase)
    {
        switch (phase)
        {
            case FramePhase::Simulate:      return "simulate";
            case FramePhase::BeginFrame:    return "begin_frame";
            case FramePhase::Clear:         return "clear";
            case FramePhase::RenderScene:   return "render_scene";
            case FramePhase::RenderOverlay: return "render_overlay";
            case FramePhase::EndFrame:      return "end_frame";
            case FramePhase::Present:       return "present";
            case FramePhase::Count:         break;
        }
        return "<invalid>";
    }

    namespace
    {
        // Shared across all nodes for one run(); lives on the stack of run().
        struct RunState
        {
            const FramePhaseOps* ops     = nullptr;
            bool                 stopped = false;
            FramePhase           failed  = FramePhase::Count;
        };

        const std::function<bool()>& phase_op(const FramePhaseOps& ops, FramePhase p)
        {
            switch (p)
            {
                case FramePhase::Simulate:      return ops.simulate;
                case FramePhase::BeginFrame:    return ops.begin_frame;
                case FramePhase::Clear:         return ops.clear;
                case FramePhase::RenderScene:   return ops.render_scene;
                case FramePhase::RenderOverlay: return ops.render_overlay;
                case FramePhase::EndFrame:      return ops.end_frame;
                case FramePhase::Present:       return ops.present;
                case FramePhase::Count:         break;
            }
            // Unreachable for valid handles; return a stable empty callable.
            static const std::function<bool()> none;
            return none;
        }

        // The single job body for every node. The node handle is the phase index
        // (nodes are added in FramePhase order), so one function serves all phases.
        void run_phase(jobs::JobContext& ctx)
        {
            auto* state = static_cast<RunState*>(ctx.frame_user);
            if (state->stopped)
                return;  // a prior phase failed — skip the rest of the frame

            const FramePhase p = static_cast<FramePhase>(ctx.node);
            const std::function<bool()>& op = phase_op(*state->ops, p);

            // An unset phase is a no-op that succeeds.
            if (op && !op())
            {
                state->stopped = true;
                state->failed  = p;
            }
        }
    }

    FrameSchedule::FrameSchedule()
    {
        jobs::NodeHandle prev = jobs::INVALID_JOB;
        for (uint32_t i = 0; i < kFramePhaseCount; ++i)
        {
            const FramePhase phase = static_cast<FramePhase>(i);
            const jobs::NodeHandle h = graph_.add_job({
                .name = to_string(phase),
                .lane = jobs::ExecutionLane::MainThread,
                .run  = &run_phase,
            });
            // The node-handle == phase-index invariant run_phase relies on.
            assert(h == static_cast<jobs::NodeHandle>(i));
            if (prev != jobs::INVALID_JOB)
                graph_.add_dependency(prev, h);
            prev = h;
        }

        const bool committed = graph_.commit();
        assert(committed && "FrameSchedule phase chain must be acyclic");
        (void)committed;

        exec_.reset(graph_);
    }

    FrameSchedule::Result FrameSchedule::run(const FramePhaseOps& ops)
    {
        exec_.reset(graph_);

        RunState state;
        state.ops = &ops;
        for (uint32_t i = 0; i < kFramePhaseCount; ++i)
            exec_.bind(static_cast<jobs::NodeHandle>(i), &state);

        if (profiling_)
        {
            profile_.reset(frame_index_++);
            sched_.set_profile(&profile_);
        }

        sched_.execute(graph_, exec_);

        if (profiling_)
        {
            sched_.set_profile(nullptr);
            analyze_critical_path(graph_, profile_, analysis_);
        }

        Result r;
        r.ok           = !state.stopped;
        r.failed_phase = state.stopped ? state.failed : FramePhase::Count;
        return r;
    }
}
